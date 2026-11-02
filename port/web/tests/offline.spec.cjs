const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const path = require('path');
const o = require('./offline_helpers.cjs');
const h = require('./disc_helpers.cjs');
let site;
test.beforeEach(async ({page}, info) => {
  site = new o.Site(info.project.name); await site.start(); await site.prepare(page);
});
test.afterEach(async () => { await site.close(); });
test('program cache is complete, isolated and strictly scoped offline', async ({page, context}) => {
  await page.goto('/'); await o.controlled(page);
  const keys = await page.evaluate(async () => {
    const cache = await caches.open((await caches.keys())[0]);
    return (await cache.keys()).map(r => new URL(r.url).pathname).sort();
  });
  expect(keys).toEqual(['/', '/audio_worklet.js', '/dw1.js', '/dw1.wasm', '/dw1_pack.js', '/dw1_pack.wasm', '/io_worker.js', '/us.tsv', '/version.txt'].sort());
  await context.setOffline(true); await site.stop();
  await page.goto('/?offline=1');
  // Query navigation uses the cached Shell; the diagnostic query path is not the Launcher.
  expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);
  await page.goto('/index.html');
  expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);
  await expect(page.locator('#instance')).toBeHidden();
  await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
  await expect(page.locator('#status')).toHaveText('');
  await expect(page.locator('#play')).toBeDisabled();
  const routes = await page.evaluate(async () => {
    const result = {};
    for (const url of ['/us.tsv', '/dw1_pack.js', '/dw1_pack.wasm', '/dw1.js?poison=1', '/__fixture/us.pack', '/isolated-off/', '/seed.html', '/dw1.html']) {
      try { result[url] = (await fetch(url)).ok; } catch (_) { result[url] = false; }
    }
    return result;
  });
  expect(routes).toEqual({'/us.tsv':true, '/dw1_pack.js':true, '/dw1_pack.wasm':true,
    '/dw1.js?poison=1':false, '/__fixture/us.pack':false, '/isolated-off/':false, '/seed.html':false, '/dw1.html':false});
  // WORKERFS and the C pack builder remain usable without the server.
  await page.locator('#disc').setInputFiles([h.bin(h.disc())]); await o.ready(page);
});
test('first visit starts from the installed cache without downloading again', async ({page, context}) => {
  await o.build(page);
  await context.setOffline(true); await site.stop();
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => typeof window.Module?.control)).toBe('function');
  await expect(page.locator('#status')).not.toContainText('No se pudo descargar el juego');
  await expect(page.locator('#audio-warning')).toHaveText('');
});
test('twenty offline reloads keep Play independent of network', async ({page, context, browserName}) => {
  await o.build(page); await context.setOffline(true); await site.stop();
  const timings = [];
  try {
    for (let i = 0; i < 20; i++) {
      const start = Date.now();
      await page.reload();
      try { await o.ready(page); }
      finally {
        timings.push({iteration:i, ms:Date.now() - start, ...await page.evaluate(() => ({
          isolated:crossOriginIsolated, instance:document.querySelector('#instance').hidden,
          pack:document.querySelector('#pack-status').textContent, status:document.querySelector('#status').textContent,
          play:!document.querySelector('#play').disabled}))});
      }
      expect(timings.at(-1).ms).toBeLessThanOrEqual(5000);
    }
    await page.locator('#play').click();
    await expect.poll(() => page.evaluate(() => typeof window.Module?.control)).toBe('function');
    await expect(page.locator('#status')).not.toContainText('No se pudo descargar el juego');
    await expect(page.locator('#audio-warning')).toHaveText('');
  } finally { h.evidence(browserName, 'offline-reloads', {timings}); }
});
test('untouched update reloads once, preserves data and reloads lock waiters', async ({page, context}) => {
  await o.build(page);
  const before = await o.opfs(page, 'read');
  await page.evaluate(async () => {
    await dwStore('settings', 'readwrite', s => s.put({sentinel:'settings'}, 'offline-test'));
    await dwStore('cards', 'readwrite', s => s.put(new Uint8Array(131072).fill(73).buffer, 0));
  });
  site.gate();
  const source = site.publish('two');
  expect(site.publish('two')).toBe(source);
  let loads = 0; page.on('load', () => loads++);
  await page.reload(); await o.ready(page);
  const waiter = await context.newPage(); await waiter.goto('/');
  await expect(waiter.locator('#instance')).toBeVisible();
  let waiterLoads = 0; waiter.on('load', () => waiterLoads++);
  site.release();
  await expect.poll(() => loads).toBe(2);
  await expect.poll(() => waiterLoads).toBe(1);
  // Both reload on activation; either can acquire the lock first after navigation.
  await waiter.close();
  await expect(page.locator('#version')).toHaveText('two', {timeout:30000}); await o.ready(page);
  await expect.poll(() => o.cachesAt(page)).toHaveLength(1);
  for (const url of ['/', '/dw1.js', '/io_worker.js', '/audio_worklet.js']) {
    const source = await page.evaluate(async url => (await fetch(url)).text(), url);
    expect(source).toContain('DW1 offline test version: two');
    expect(source).not.toContain('DW1 offline test version: one');
  }
  expect(await page.content()).toContain('DW1 offline test version: two');
  expect(await o.opfs(page, 'read')).toEqual(before);
  expect(await page.evaluate(async () => ({
    settings:await dwStore('settings', 'readonly', s => s.get('offline-test')),
    card:[...new Uint8Array(await dwStore('cards', 'readonly', s => s.get(0)))]
  }))).toEqual({settings:{sentinel:'settings'}, card:Array(131072).fill(73)});
  const sameLoads = loads;
  await page.evaluate(async () => (await navigator.serviceWorker.getRegistration()).update());
  expect(loads).toBe(sameLoads);
});
test('Play during activation reloads before the game main starts', async ({page}) => {
  await o.build(page);
  await page.addInitScript(() => {
    const post = ServiceWorker.prototype.postMessage;
    const held = [];
    ServiceWorker.prototype.postMessage = function(...args) {
      if (args[0] === 'activate') held.push(() => post.apply(this, args));
      else return post.apply(this, args);
    };
    window.activationHeld = () => held.length > 0;
    window.releaseActivation = () => held.splice(0).forEach(send => send());
  });
  site.publish('two');
  let loads = 0; page.on('load', () => loads++);
  await page.reload(); await o.ready(page);
  await expect.poll(() => page.evaluate(() => window.activationHeld())).toBe(true);
  await page.evaluate(() => {
    document.querySelector('#play').click();
    window.releaseActivation();
  });
  await expect.poll(() => loads).toBe(2);
  await expect(page.locator('#version')).toHaveText('two', {timeout:30000});
  await o.ready(page);
  await expect(page.locator('#game')).toBeHidden();
  expect(await page.evaluate(() => typeof window.Module?.control)).toBe('undefined');
});
for (const interaction of ['pointerdown', 'keydown', 'dragenter', 'game']) test('defers update after ' + interaction, async ({page, context}) => {
  await o.build(page);
  site.gate(); site.publish('two');
  await page.reload(); await o.ready(page);
  // The installed worker is held at dw1.wasm while the user acts.
  if (interaction === 'game') await page.locator('#play').click();
  else await page.evaluate(event => dispatchEvent(new Event(event)), interaction);
  site.release(); await o.waiting(page);
  await expect(page.locator('#update-status')).toContainText('próxima carga');
  await expect(page.locator('#version')).toHaveText('one');
  if (interaction === 'game') await expect(page.locator('#game')).toBeVisible();
  // A completely installed update can also activate on the next offline load.
  if (interaction === 'keydown') { await context.setOffline(true); await site.stop(); }
  await page.reload(); await o.ready(page);
  await expect(page.locator('#version')).toHaveText('two', {timeout:30000});
});
test('incomplete install preserves old cache and retries next load', async ({page}) => {
  await o.build(page); const old = await o.cachesAt(page);
  site.fail('/dw1_pack.wasm'); site.publish('two');
  await page.reload(); await o.ready(page);
  await expect(page.locator('#version')).toHaveText('one');
  await expect.poll(() => fs.existsSync(path.join(site.dir, 'fail-hit'))).toBe(true);
  await expect.poll(() => o.cachesAt(page)).toEqual(old);
  fs.rmSync(path.join(site.dir, 'fail'));
  await page.reload(); await expect(page.locator('#version')).toHaveText('two', {timeout:30000}); await o.ready(page);
});
test('migrates v0 offline to byte-identical v1 without disc', async ({page, context}) => {
  await o.build(page); const before = await o.opfs(page, 'read');
  await o.opfs(page, 'version', 0);
  await context.setOffline(true); await site.stop();
  await page.addInitScript(() => {
    window.migrationProgressShown = false;
    new MutationObserver(() => {
      if (document.querySelector('#pack-status')?.textContent === 'Pack desactualizado' &&
          document.querySelector('#progress')?.hidden === false)
        window.migrationProgressShown = true;
    }).observe(document, {subtree:true, childList:true, attributes:true});
  });
  await page.reload(); await o.ready(page);
  expect(await page.evaluate(() => window.migrationProgressShown)).toBe(true);
  expect(await o.opfs(page, 'read')).toEqual(before);
  expect(await h.entries(page)).toEqual(['us.pack', 'us.pack.sha256']);
});
for (const mode of ['corrupt', 'quota', 'newer']) test('preserves Pack on ' + mode, async ({page, context}) => {
  await o.build(page); const canonical = await o.opfs(page, 'read'); await o.opfs(page, 'version', mode === 'newer' ? 2 : 0);
  if (mode === 'corrupt') await o.opfs(page, 'corrupt', 0);
  const before = await o.opfs(page, 'read');
  if (mode === 'quota') await page.addInitScript(() => {
    const Native = Worker;
    window.Worker = class extends Native {
      constructor(url, options) {
        if (url === '/io_worker.js') {
          const prefix = `const original = FileSystemSyncAccessHandle.prototype.write;
            FileSystemSyncAccessHandle.prototype.write = function(...args) {
              if (this.getSize() === 0) throw new DOMException('quota test', 'QuotaExceededError');
              return original.apply(this, args);
            }; importScripts(${JSON.stringify(location.origin + '/io_worker.js')});`;
          url = URL.createObjectURL(new Blob([prefix], {type:'text/javascript'}));
        }
        super(url, options);
      }
    };
  });
  await page.reload(); await expect(page.locator('#disc')).toBeEnabled();
  await expect(page.locator('#pack-status')).toHaveText(mode === 'newer' ? 'Pack de una versión más nueva del port' : 'Pack desactualizado');
  if (mode !== 'newer') await expect(page.locator('#status')).not.toBeEmpty();
  await expect(page.locator('#play')).toBeDisabled();
  expect(await o.opfs(page, 'read')).toEqual(before);
  await expect(page.locator('#clear')).toBeEnabled();
  if (mode === 'quota') {
    await page.close();
    const retry = await context.newPage(); await retry.goto('/'); await o.ready(retry);
    expect(await o.opfs(retry, 'read')).toEqual(canonical);
    await retry.close();
  } else if (mode === 'corrupt') {
    await page.reload(); await expect(page.locator('#disc')).toBeEnabled();
    await expect(page.locator('#pack-status')).toHaveText('Pack desactualizado');
    await expect(page.locator('#status')).not.toBeEmpty();
    expect(await o.opfs(page, 'read')).toEqual(before);
  } else {
    await page.locator('#clear').click(); await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
    expect(await h.entries(page)).toEqual([]);
  }
});
for (const state of ['previous', 'leftovers']) test('recovers interrupted replacement: ' + state, async ({page}) => {
  await o.build(page); const before = await o.opfs(page, 'read');
  await o.opfs(page, state); await page.reload(); await o.ready(page);
  expect(await o.opfs(page, 'read')).toEqual(before);
  expect(await h.entries(page)).toEqual(['us.pack', 'us.pack.sha256']);
});
for (const mode of ['absent', 'reject', 'undefined']) test('works without service worker: ' + mode, async ({page}) => {
  await page.addInitScript(mode => {
    if (mode === 'absent') Object.defineProperty(navigator, 'serviceWorker', {value:undefined});
    else navigator.serviceWorker.register = mode === 'reject' ? async () => {throw Error('register refused');} : async () => undefined;
  }, mode);
  await page.goto('/'); await expect(page.locator('#disc')).toBeEnabled();
  await expect(page.locator('#update-status')).toContainText('sin conexión no disponible');
  await page.locator('#disc').setInputFiles([h.bin(h.disc())]); await o.ready(page);
});
test('Pack version constant agrees with native header', () => {
  const root = path.resolve(__dirname, '../../..');
  const header = fs.readFileSync(path.join(root, 'port/host/pack/pack.h'), 'utf8');
  const worker = fs.readFileSync(path.join(root, 'port/web/io_worker.js'), 'utf8');
  expect(worker.match(/const PACK_FORMAT_VERSION = (\d+)/)[1]).toBe(header.match(/#define PACK_FORMAT_VERSION (\d+)/)[1]);
});

test('first failed installation leaves online Launcher usable and retries next load', async ({page}) => {
  site.fail('/dw1.wasm');
  await page.goto('/'); await expect(page.locator('#disc')).toBeEnabled();
  await expect.poll(() => fs.existsSync(path.join(site.dir, 'fail-hit'))).toBe(true);
  await expect.poll(() => o.cachesAt(page)).toEqual([]);
  await page.locator('#disc').setInputFiles([h.bin(h.disc())]); await o.ready(page);
  fs.rmSync(path.join(site.dir, 'fail'));
  await page.reload(); await o.ready(page); await o.controlled(page);
  expect(await o.cachesAt(page)).toHaveLength(1);
});
test('slow optional metadata cannot keep Play disabled', async ({page}) => {
  fs.writeFileSync(path.join(site.dir, 'metadata-gate'), '');
  await page.goto('/'); await expect(page.locator('#disc')).toBeEnabled();
  await page.locator('#disc').setInputFiles([h.bin(h.disc())]); await o.ready(page);
  fs.rmSync(path.join(site.dir, 'metadata-gate'));
  await o.controlled(page);
});
