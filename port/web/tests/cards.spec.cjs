const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('node:fs');
const {seed, store, errors} = require('./storage.cjs');
const h = require('./cards_helpers.cjs');

// Class A must not compare its synthetic Pack with a real Pack left by class B.
test.beforeEach(async ({page}) => {
  await page.route('**/__fixture/us.pack.sha256', route => route.fulfill({status:404, body:''}));
});

// Observe the browser transaction option, rather than merely checking the application's code.
async function observe(page, mode) {
  await page.addInitScript(mode => {
    window.cardTransactions = [];
    const transaction = IDBDatabase.prototype.transaction;
    IDBDatabase.prototype.transaction = function(names, access, options) {
      const tx = transaction.call(this, names, access, options);
      if (names === 'cards' && access === 'readwrite') {
        cardTransactions.push(options?.durability);
        if (mode === 'quota' && !window.quotaInjected) {
          window.quotaInjected = true;
          const objectStore = tx.objectStore.bind(tx);
          tx.objectStore = name => {
            const object = objectStore(name);
            object.put = () => { throw new DOMException('full', 'QuotaExceededError'); };
            return object;
          };
        }
      }
      return tx;
    };
    const put = IDBObjectStore.prototype.put;
    IDBObjectStore.prototype.put = function(...args) {
      const result = put.apply(this, args);
      if (this.name === 'cards' && mode === 'abort') this.transaction.abort();
      if (this.name === 'cards' && mode === 'close') console.log('card-put-close');
      return result;
    };
  }, mode);
}

test('empty cards stay unpersisted, commits preserve the last consistent bytes and export them', async ({page}) => {
  const failures = errors(page);
  await observe(page);
  await h.shell(page);
  for (const port of [1, 2]) {
    await expect(page.locator(`#card-slot-${port}`)).toContainText('Vacía');
    await expect(page.locator(`#card-slot-${port} button`)).toBeDisabled();
  }
  expect(await h.card(page)).toBeNull();
  await page.goto('/?cards=1&frames=1&pattern=1'); await h.finished(page);
  const blanks = await page.evaluate(() => [0, 1].map(port => {
    const address = Module._web_card_address(port) + 4;
    return Array.from(HEAPU8.subarray(address, address + 131072));
  }));
  for (const bytes of blanks) expect(h.sha(Buffer.from(bytes))).toBe('78b6d4ac9ab4d23caf7e5f04f83539bf5d994cccfb0a709d14ac53d05c8e21ef');
  expect(await h.card(page)).toBeNull(); expect(await h.card(page, 1)).toBeNull();
  await page.goto('/?cards=1&card-test=40&poison=1');
  await h.finished(page);
  await expect.poll(() => page.evaluate(() => Module.cards.stats())).toEqual([
    {seq:80, persisted:80, failures:0}, {seq:80, persisted:80, failures:0}
  ]);
  expect(await h.card(page)).toEqual(h.pattern(40));
  expect(await h.card(page, 1)).toEqual(h.pattern(168));
  const durability = await page.evaluate(() => cardTransactions);
  expect(durability.length).toBeGreaterThan(0); expect(durability.every(x => x === 'strict')).toBe(true);
  expect(await page.evaluate(() => Module.storageAccesses)).toBe(0);
  await h.shell(page);
  await expect(page.locator('#card-slot-1')).toContainText('Con datos');
  await expect(page.locator('#card-slot-2')).toContainText('Con datos');
  expect(await h.card(page, 0)).toEqual(h.pattern(40));
  expect(await h.card(page, 1)).toEqual(h.pattern(168));
  expect(await h.exported(page)).toEqual(h.pattern(40));
  expect(await h.exported(page, 2)).toEqual(h.pattern(168));
  expect(failures).toEqual([]);
});

test('import validates all sizes, confirms replacement, preserves the other slot and requests persistence', async ({page}) => {
  await page.addInitScript(() => {navigator.storage.persist = async () => false;});
  await h.shell(page);
  for (const length of [0, 131071, 131073, 131136]) {
    await h.importCard(page, Buffer.alloc(length));
    await expect(page.locator('#card-status')).toContainText(String(length));
    expect(await h.card(page)).toBeNull();
  }
  await h.importCard(page, h.pattern(9), 2, 'save.mcr');
  await expect(page.locator('#card-slot-2')).toContainText('Con datos');
  await expect(page.locator('#storage-warning')).toContainText('denegado');
  await expect(page.locator('#storage-warning')).toContainText('exportar la Memory card');
  await h.importCard(page, h.pattern(1));
  await expect(page.locator('#card-slot-1')).toContainText('Con datos');
  page.once('dialog', d => d.dismiss());
  await h.importCard(page, h.pattern(2));
  await expect(page.locator('#card-slot-1 input')).toBeEnabled();
  expect(await h.card(page)).toEqual(h.pattern(1));
  page.once('dialog', d => d.accept());
  await h.importCard(page, h.pattern(3));
  await expect.poll(async () => h.sha(await h.card(page))).toBe(h.sha(h.pattern(3)));
  expect(await h.exported(page)).toEqual(h.pattern(3));
  expect(await h.card(page, 1)).toEqual(h.pattern(9));
});

test('starting the game waits for an import already started in the Shell', async ({page}) => {
  await seed(page, 'synthetic.pack', h.sha(fs.readFileSync(`${process.env.DW1_WEB_BUILD}/synthetic.pack`)));
  await page.route('**/dw1.js', async route => {
    // Use the real runtime and C attachment, stopping only at the game's entry point.
    await page.evaluate(() => {
      const initialized = Module.onRuntimeInitialized;
      Module.onRuntimeInitialized = async () => {
        Module.callMain = args => {
          const address = Module._card_image_data(0);
          window.gameArguments = args;
          window.gameCard = address ? Array.from(HEAPU8.subarray(address, address + 131072)) : null;
        };
        window.cardRuntimeReady = true;
        return initialized();
      };
    });
    await route.continue();
  });
  await h.shell(page);
  await page.evaluate(() => {
    const read = File.prototype.arrayBuffer;
    File.prototype.arrayBuffer = function() {
      return new Promise(resolve => { window.finishImport = () => resolve(read.call(this)); });
    };
  });
  await h.importCard(page, h.pattern(11));
  await expect(page.locator('#card-slot-1 input')).toBeDisabled();
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => window.cardRuntimeReady)).toBe(true);
  expect(await page.evaluate(() => window.gameCard)).toBeUndefined();
  await page.evaluate(() => finishImport());
  await expect.poll(() => page.evaluate(() => window.gameCard?.length)).toBe(131072);
  expect(await page.evaluate(() => window.gameArguments)).toEqual(['--frames', '0', '--pack', 'dw1/us.pack']);
  expect(Buffer.from(await page.evaluate(() => window.gameCard))).toEqual(h.pattern(11));
  expect(await h.card(page)).toEqual(h.pattern(11));
});

test('v1 database migrates without changing PortSettings; newer database is protected', async ({page}) => {
  await page.goto('/seed.html');
  await store(page, {schema_version:99, extra:'preserved'});
  await h.shell(page);
  expect(await store(page)).toEqual({schema_version:99, extra:'preserved'});
  expect(await h.card(page)).toBeNull();
  await page.goto('/seed.html');
  await page.evaluate(() => new Promise((resolve, reject) => {
    const open = indexedDB.open('dw1', 3);
    open.onerror = () => reject(open.error);
    open.onsuccess = () => {open.result.close(); resolve();};
  }));
  await h.shell(page);
  await expect(page.locator('#settings-warning')).toContainText('defecto');
  await expect(page.locator('#card-slot-1')).toContainText('Ilegible');
  await expect(page.locator('#card-warning')).not.toBeEmpty();
  await page.goto('/?cards=1&card-test=1');
  await expect.poll(() => page.evaluate(() => window.Module?.finished)).toBe(1);
  expect(await h.card(page)).toBeNull();
});

for (const invalid of ['size', 'type']) test(`unreadable card (${invalid}) is absent and only explicit import replaces it`, async ({page}) => {
  await h.shell(page);
  await page.evaluate(invalid => dwStore('cards', 'readwrite', s => s.put(invalid === 'size' ? new ArrayBuffer(1) : 'bad', 0)), invalid);
  await h.shell(page);
  await expect(page.locator('#card-slot-1')).toContainText('Ilegible');
  await page.goto('/?cards=1&card-test=2');
  await expect.poll(() => page.evaluate(() => window.Module?.finished)).toBe(1);
  const original = await page.evaluate(() => dwStore('cards', 'readonly', s => s.get(0)).then(x => typeof x === 'string' ? x : x.byteLength));
  expect(original).toBe(invalid === 'size' ? 1 : 'bad');
  await h.shell(page);
  page.once('dialog', d => d.dismiss()); await h.importCard(page, h.pattern(1));
  await expect(page.locator('#card-slot-1 input')).toBeEnabled();
  await expect(page.locator('#card-slot-1')).toContainText('Ilegible');
  page.once('dialog', d => d.accept()); await h.importCard(page, h.pattern(1));
  await expect(page.locator('#card-slot-1')).toContainText('Con datos');
  expect(await h.card(page)).toEqual(h.pattern(1));
});

test('quota failure is visible, does not stop the game and next command retries', async ({page}) => {
  await observe(page, 'quota');
  await page.goto('/?cards=1&card-test=20');
  await h.finished(page); await h.persisted(page, 40);
  await expect(page.locator('#card-warning')).toContainText('full');
  await expect(page.locator('#card-warning')).not.toContainText('exportar');
  expect((await page.evaluate(() => Module.cards.stats()[0])).failures).toBe(1);
  expect(await h.card(page)).toEqual(h.pattern(20));
});

for (const mode of ['abort', 'close']) test(`interruption after put (${mode}), five independent cuts`, async ({page, context}) => {
  test.setTimeout(120000);
  await h.shell(page);
  await h.card(page, 0, h.pattern(90));
  // Keep a same-origin reader alive without taking the game lock.
  const reader = await context.newPage(); await reader.goto('/seed.html');
  for (let n = 0; n < 5; n++) {
    await h.card(reader, 0, h.pattern(90));
    const target = await context.newPage();
    await observe(target, mode);
    let closed;
    if (mode === 'close') closed = new Promise(resolve => target.on('console', m => {
      if (m.text() === 'card-put-close') target.close().then(resolve);
    }));
    // Release the original Shell lock before this writer starts.
    if (n === 0) await page.goto('/seed.html');
    await target.goto('/?cards=1&card-test=1');
    if (mode === 'close') await closed;
    else {
      await h.finished(target);
      await expect(target.locator('#card-warning')).not.toBeEmpty();
      await target.close();
    }
    const bytes = await h.card(reader);
    expect([h.sha(h.pattern(90)), h.sha(h.pattern(1))]).toContain(h.sha(bytes));
    if (mode === 'abort') expect(bytes).toEqual(h.pattern(90));
  }
  await reader.close();
});

test('leaving the page persists the latest snapshot when every notification is held, five times', async ({page}) => {
  test.setTimeout(120000);
  for (let n = 0; n < 5; n++) {
    const iterations = 10 + n;
    await page.goto(`/?cards=1&card-test=${iterations}&card-hold=1`);
    await h.finished(page);
    expect(await page.evaluate(() => Module.cards.stats()[0].persisted)).toBe(0);
    for (const port of [0, 1]) {
      expect(await h.card(page, port)).toEqual(n ? h.pattern(iterations - 1 + port * 128) : null);
    }
    await page.goto('/seed.html');
    for (const port of [0, 1]) {
      await expect.poll(async () => { const bytes = await h.card(page, port); return bytes && h.sha(bytes); })
        .toBe(h.sha(h.pattern(iterations + port * 128)));
    }
  }
});

test('pagehide bounded seqlock retry keeps the previous consistent command', async ({page}) => {
  await page.goto('/?cards=1&card-test=1&card-hold=1'); await h.finished(page);
  await page.evaluate(() => dispatchEvent(new Event('pagehide'))); await h.persisted(page, 2);
  await page.evaluate(() => {
    const address = Module._web_card_address(0);
    Atomics.store(new Int32Array(HEAPU8.buffer, address, 1), 0, 3);
    HEAPU8.fill(0xff, address + 4, address + 4 + 131072);
    dispatchEvent(new Event('pagehide'));
  });
  expect(await h.card(page)).toEqual(h.pattern(1));
});

test('twenty synthetic Pack reloads enable Jugar within five seconds and clearing keeps cards', async ({page}, info) => {
  test.setTimeout(120000);
  const evidence = [];
  await seed(page, 'synthetic.pack', h.sha(fs.readFileSync(`${process.env.DW1_WEB_BUILD}/synthetic.pack`)));
  await page.addInitScript(() => {navigator.storage.persist = async () => false;});
  await h.shell(page); await h.card(page, 0, h.pattern(7));
  try {
    for (let n = 0; n < 20; n++) {
      const start = Date.now(); await page.reload();
      try {
        await expect(page.locator('#instance')).toBeHidden();
        await expect(page.locator('#pack-status')).toHaveText('Listo para jugar', {timeout:5000});
        await expect(page.locator('#status')).toHaveText('');
        await expect(page.locator('#play')).toBeEnabled({timeout:5000});
      } finally {
        evidence.push({iteration:n, ms:Date.now() - start,
          instance:await page.locator('#instance').evaluate(x => ({hidden:x.hidden, text:x.textContent})),
          pack_status:await page.locator('#pack-status').textContent(), status:await page.locator('#status').textContent(),
          play_enabled:await page.locator('#play').isEnabled()});
      }
      expect(evidence.at(-1).ms).toBeLessThanOrEqual(5000);
      await expect(page.locator('#storage-warning')).toContainText('exportar la Memory card');
    }
    await page.locator('#clear').click();
    await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
    expect(await h.card(page)).toEqual(h.pattern(7));
  } finally { fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/cards-reloads-${info.project.name}.json`, JSON.stringify(evidence, null, 2)); }
});

test('an unreadable card is protected and card reads never gate Jugar', async ({page}) => {
  await seed(page, 'synthetic.pack', h.sha(fs.readFileSync(`${process.env.DW1_WEB_BUILD}/synthetic.pack`)));
  await h.shell(page); await h.card(page, 0, h.pattern(80));
  await page.addInitScript(() => {
    const get = IDBObjectStore.prototype.get;
    window.allowCardReads = false;
    IDBObjectStore.prototype.get = function(key) {
      if (!allowCardReads && this.name === 'cards' && key === 0) throw new DOMException('denied', 'SecurityError');
      return get.call(this, key);
    };
  });
  await h.shell(page);
  await expect(page.locator('#card-slot-1')).toContainText('Ilegible');
  await expect(page.locator('#card-slot-2')).toContainText('Vacía');
  await expect(page.locator('#card-warning')).toContainText('denied');
  await expect(page.locator('#play')).toBeEnabled();
  await page.goto('/?cards=1&card-test=2');
  await expect.poll(() => page.evaluate(() => window.Module?.finished)).toBe(1);
  await page.evaluate(() => {window.allowCardReads = true;});
  expect(await h.card(page)).toEqual(h.pattern(80));
  // A stalled card read must not stall OPFS status or enablement of Jugar either.
  await page.addInitScript(() => {
    const transaction = IDBDatabase.prototype.transaction;
    IDBDatabase.prototype.transaction = function(name, ...args) {
      if (name === 'cards') return {objectStore:() => ({get:() => ({})})};
      return transaction.call(this, name, ...args);
    };
  });
  await page.goto('/');
  await expect(page.locator('#play')).toBeEnabled();
});
