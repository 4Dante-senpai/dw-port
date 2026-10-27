const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const {errors} = require('./storage.cjs');
const h = require('./disc_helpers.cjs');
const canonical = 'c01e5c3680ffdf50d0342f16ec978c6d107a33529688dae6a817bd63947f0cf6';
test('own disc produces the native Pack, persists and clears', async ({page, browserName}) => {
  test.setTimeout(1500000);
  const failures = errors(page);
  const dir = process.env.DW1_DISC_DIR;
  const names = fs.readdirSync(dir).filter(n => /\.(cue|bin)$/i.test(n));
  expect(names.some(n => /\.bin$/i.test(n))).toBe(true);
  const native = crypto.createHash('sha256');
  for await (const chunk of fs.createReadStream(path.join(process.env.DW1_WEB_BUILD, 'us.pack')))
    native.update(chunk);
  const nativeSha = native.digest('hex'); expect(nativeSha).toBe(canonical);
  await page.addInitScript(() => {navigator.storage.persist = async () => false;});
  await h.launch(page);
  await h.memoryStart(page, browserName);
  const start = Date.now();
  await page.locator('#disc').setInputFiles(names.map(n => path.join(dir, n)), {timeout:180000});
  await expect(page.locator('#progress')).toBeVisible();
  let lastReport = start;
  await expect.poll(async () => {
    if (Date.now() - lastReport > 30000) {
      lastReport = Date.now();
      console.log(browserName, await page.locator('#progress').evaluate(p => ({read:p.value, total:p.max})),
        'elapsed_ms', Date.now() - start);
    }
    const error = await page.locator('#status').textContent();
    return error ? 'error' : page.locator('#pack-status').textContent();
  }, {timeout:1200000, intervals:[1000, 5000]}).toMatch(/^(Listo para jugar|error)$/);
  await expect(page.locator('#status')).toHaveText('');
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  const sm4 = Date.now() - start;
  const memory = await h.memoryEnd(page, browserName);
  const hashes = await page.evaluate(() => new Promise((resolve, reject) => {
    const worker = new Worker('/disc_hash_worker.js');
    worker.onmessage = ({data}) => {worker.terminate(); data.error ? reject(Error(data.error)) : resolve(data);};
    worker.onerror = e => {worker.terminate(); reject(Error(e.message));};
    worker.postMessage({});
  }));
  expect(hashes).toEqual({sha256:nativeSha, sidecar:nativeSha});
  h.evidence(browserName, 'disc-real', {native_sha256:nativeSha, ...hashes,
    memory_max_bytes:memory, sm4_ms:sm4});
  await page.reload(); await expect(page.locator('#play')).toBeEnabled();
  await page.locator('#clear').click();
  await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
  expect(await h.entries(page)).toEqual([]);
  await page.reload(); await expect(page.locator('#play')).toBeDisabled();
  expect(failures).toEqual([]);
});
