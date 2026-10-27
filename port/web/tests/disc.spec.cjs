const {test, expect} = require('/opt/node_modules/@playwright/test');
const {errors} = require('./storage.cjs');
const h = require('./disc_helpers.cjs');
const bytes = h.disc();
const replacement = Buffer.from(bytes); replacement[30 * 2352 + 24] ^= 1;
let tsv, replacementTsv;
const snapshot = page => page.evaluate(async () => {
  const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
  // Only the small synthetic Pack is buffered, never a user's disc.
  return {bytes:[...new Uint8Array(await (await (await dir.getFileHandle('us.pack')).getFile()).arrayBuffer())],
    sidecar:await (await (await dir.getFileHandle('us.pack.sha256')).getFile()).text()};
});
test.beforeAll(async ({browser}) => {
  const page = await browser.newPage();
  tsv = await h.manifest(page, bytes);
  replacementTsv = await h.manifest(page, replacement);
  expect(replacementTsv).not.toBe(tsv);
  await page.close();
});
for (const mode of ['bin', 'cue', 'drop']) test('build, reload and clear: ' + mode, async ({page, browserName}) => {
  test.setTimeout(180000);
  const failures = errors(page);
  await page.addInitScript(() => {navigator.storage.persist = async () => false;});
  await h.launch(page, tsv);
  if (mode === 'bin') await h.memoryStart(page, browserName, true);
  const files = [h.bin(bytes), ...(mode === 'bin' ? [] : [h.cue()])];
  if (mode === 'drop') {
    await page.locator('#drop-zone').evaluate((node, files) => {
      const dataTransfer = new DataTransfer();
      for (const f of files) dataTransfer.items.add(new File([new Uint8Array(f.bytes)], f.name));
      node.dispatchEvent(new DragEvent('drop', {bubbles:true, dataTransfer}));
    }, files.map(f => ({name:f.name, bytes:[...f.buffer]})));
  } else await page.locator('#disc').setInputFiles(files);
  await expect(page.locator('#progress')).toBeVisible();
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  await expect(page.locator('#play')).toBeEnabled();
  await expect(page.locator('#storage-warning')).toContainText('denegado');
  expect(await h.entries(page)).toEqual(['us.pack', 'us.pack.sha256']);
  if (mode === 'bin') h.evidence(browserName, 'disc', {memory_max_bytes:await h.memoryEnd(page, browserName)});
  await page.reload();
  await expect(page.locator('#play')).toBeEnabled();
  await page.locator('#clear').click();
  await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
  expect(await h.entries(page)).toEqual([]);
  expect(failures).toEqual([]);
});
const system = Buffer.from(bytes); system[25 * 2352 + 24] ^= 1;
const exe = Buffer.from(bytes); exe[22 * 2352 + 24] ^= 1;
const iso = Buffer.alloc(32 * 2048); iso.write('\x01CD001', 16 * 2048, 'binary');
for (const [name, files, error] of [
  ['mode', [h.bin(bytes), h.cue('MODE1/2352')], 'MODE2/2352'],
  ['iso', [{name:'disc.iso', buffer:iso, mimeType:'application/octet-stream'}], 'XA'],
  ['file hash', [h.bin(system)], 'SYSTEM.CNF'],
  ['edition', [h.bin(exe)], 'SLUS-01032'],
  ['no bin', [h.cue()], '.bin'],
  ['ambiguous bins', [h.bin(bytes), {...h.bin(bytes), name:'other.bin'}], 'único .bin'],
  ['missing cue file', [h.bin(bytes), h.cue('MODE2/2352', 'other.bin')], 'other.bin'],
]) test('rejects ' + name, async ({page}) => {
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles(files);
  await expect(page.locator('#status')).toContainText(error);
  await expect(page.locator('#play')).toBeDisabled();
  expect(await h.entries(page)).toEqual([]);
});
test('failed replacement preserves previous Pack and sidecar', async ({page}) => {
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles([h.bin(bytes)]);
  await expect(page.locator('#play')).toBeEnabled();
  const before = await page.evaluate(async () => {
    const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
    return (await (await dir.getFileHandle('us.pack.sha256')).getFile()).text();
  });
  await page.locator('#disc').setInputFiles([h.bin(system)]);
  await expect(page.locator('#status')).toContainText('SYSTEM.CNF');
  await expect(page.locator('#play')).toBeEnabled();
  expect(await h.entries(page)).toEqual(['us.pack', 'us.pack.sha256']);
  await page.reload(); await expect(page.locator('#play')).toBeEnabled();
  const after = await page.evaluate(async () => {
    const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
    return (await (await dir.getFileHandle('us.pack.sha256')).getFile()).text();
  });
  expect(after).toBe(before);
});
test('replacement is a valid disc with a different Pack hash', async ({page}) => {
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles([h.bin(bytes)]);
  await expect(page.locator('#play')).toBeEnabled();
  const before = await snapshot(page);
  await page.route('**/us.tsv', r => r.fulfill({body:replacementTsv}));
  await page.locator('#disc').setInputFiles([h.bin(replacement)]);
  await expect(page.locator('#disc')).toBeEnabled();
  await expect(page.locator('#status')).toBeEmpty();
  await expect(page.locator('#play')).toBeEnabled();
  const after = await snapshot(page);
  expect(after.bytes).not.toEqual(before.bytes);
  expect(after.sidecar).not.toBe(before.sidecar);
});
for (const [operation, target] of [
  ['write', 'us.pack.tmp'], ['write', 'us.pack.sha256.tmp'],
  ['move', 'us.pack.tmp'], ['move', 'us.pack.sha256.tmp'],
]) test('storage failure preserves prior data: ' + operation + ' ' + target, async ({page}) => {
  await page.addInitScript(() => {navigator.storage.persist = async () => false;});
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles([h.bin(bytes)]);
  await expect(page.locator('#play')).toBeEnabled();
  const before = await snapshot(page);
  await page.route('**/us.tsv', r => r.fulfill({body:replacementTsv}));
  await page.route('**/io_worker.js', async route => {
    const response = await route.fetch();
    const inject = operation === 'write' ? `const originalOpen = FileSystemFileHandle.prototype.createSyncAccessHandle;
      FileSystemFileHandle.prototype.createSyncAccessHandle = async function(...args) {
        const handle = await originalOpen.apply(this, args);
        if (this.name === ${JSON.stringify(target)}) handle.write = () => {throw Error('injected storage failure');};
        return handle;
      };\n` : `const originalMove = FileSystemFileHandle.prototype.move;
      FileSystemFileHandle.prototype.move = async function(...args) {
        if (this.name === ${JSON.stringify(target)} && args[0] === ${JSON.stringify(target.replace('.tmp', ''))})
          throw Error('injected storage failure');
        return originalMove.apply(this, args);
      };\n`;
    await route.fulfill({response, body:inject + await response.text()});
  });
  await page.locator('#disc').setInputFiles([h.bin(replacement)]);
  await expect(page.locator('#status')).toContainText('injected storage failure');
  const assertRestored = async () => {
    await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
    await expect(page.locator('#play')).toBeEnabled();
    await expect(page.locator('#disc')).toBeEnabled();
    expect(await h.entries(page)).toEqual(['us.pack', 'us.pack.sha256']);
    expect(await snapshot(page)).toEqual(before);
  };
  await assertRestored();
  await page.reload();
  await assertRestored();
  await expect(page.locator('#status')).toBeEmpty();
});
for (const outcome of ['pending', 'rejects']) test('Play does not wait for persist: ' + outcome, async ({page}) => {
  await page.addInitScript(outcome => {
    navigator.storage.persist = () => {
      window.persistCalled = true;
      return outcome === 'pending' ? new Promise(resolve => {window.resolvePersistence = resolve;}) :
        Promise.reject(Error('injected persistence rejection'));
    };
  }, outcome);
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles([h.bin(bytes)]);
  await expect(page.locator('#play')).toBeEnabled();
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  expect(await page.evaluate(() => window.persistCalled)).toBe(true);
  if (outcome === 'pending') {
    await expect(page.locator('#storage-warning')).toBeEmpty();
    await page.evaluate(() => window.resolvePersistence(false));
  }
  await expect(page.locator('#storage-warning')).toContainText(
    outcome === 'pending' ? 'denegado' : 'No se pudo solicitar almacenamiento persistente: injected persistence rejection');
  await expect(page.locator('#play')).toBeEnabled();
});
test('progress includes the SHA-256 reread before commit', async ({page}) => {
  await page.addInitScript(() => {
    window.packProgress = [];
    const NativeWorker = Worker;
    window.Worker = class extends NativeWorker {
      constructor(...args) {
        super(...args);
        this.addEventListener('message', ({data}) => {
          if (data.progress !== undefined) window.packProgress.push(data);
        });
      }
    };
  });
  await h.launch(page, tsv);
  await page.locator('#disc').setInputFiles([h.bin(bytes)]);
  await expect(page.locator('#play')).toBeEnabled();
  const progress = await page.evaluate(() => window.packProgress);
  const hashStart = progress.findIndex(p => p.phase === 'sha256');
  expect(hashStart).toBeGreaterThanOrEqual(0);
  expect(progress.slice(0, hashStart).every(p => p.progress < p.total)).toBe(true);
  expect(progress.slice(hashStart + 1, -1).some(p => p.phase === 'sha256' && p.progress > 500 && p.progress <= 999)).toBe(true);
  expect(progress.slice(0, -1).every(p => p.progress < p.total)).toBe(true);
  expect(progress.at(-1)).toEqual({progress:1000, total:1000});
});
