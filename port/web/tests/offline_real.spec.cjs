const {test, expect} = require('/opt/node_modules/@playwright/test');
const o = require('./offline_helpers.cjs');
const h = require('./disc_helpers.cjs');
const {seed, errors} = require('./storage.cjs');
const fs = require('fs');
const path = require('path');
let site;
test.beforeEach(async ({}, info) => {site = new o.Site(info.project.name, true); await site.start();});
test.afterEach(async () => {await site?.close();});
async function hash(page) {
  return page.evaluate(() => new Promise((resolve, reject) => {
    const worker = new Worker('/disc_hash_worker.js');
    worker.onmessage = ({data}) => {worker.terminate(); data.error ? reject(Error(data.error)) : resolve(data);};
    worker.onerror = e => reject(Error(e.message)); worker.postMessage({});
  }));
}
test('real Pack starts offline, runs 600 VBlanks and survives update', async ({page, context, browserName}) => {
  test.setTimeout(400000);
  const failures = errors(page);
  const sha = fs.readFileSync(path.join(site.dir, 'us.pack.sha256'), 'utf8').trim();
  await seed(page, 'us.pack', sha);
  await page.goto('/'); await o.ready(page, 'Pack disponible'); await o.controlled(page);
  const before = await hash(page); expect(before).toEqual({sha256:sha, sidecar:sha});
  await context.setOffline(true); await site.stop();
  await page.reload(); await o.ready(page);
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => Module.control?.().vblanks || 0), {timeout:90000}).toBeGreaterThan(600);
  expect(await page.evaluate(() => Module.frames.shown)).toBeGreaterThan(0);
  await expect(page.locator('#audio-warning')).toHaveText('');
  await expect(page.locator('#status')).not.toContainText(/error|abort/i);
  const vblanks = await page.evaluate(() => Module.control().vblanks);
  await context.setOffline(false); await site.start(); site.publish('two');
  await page.evaluate(async () => (await navigator.serviceWorker.getRegistration()).update());
  await o.waiting(page);
  await expect(page.locator('#update-status')).toContainText('próxima carga');
  await expect.poll(() => page.evaluate(() => Module.control().vblanks)).toBeGreaterThan(vblanks);
  await expect(page.locator('#game')).toBeVisible();
  await context.setOffline(true); await site.stop();
  await page.reload();
  await expect(page.locator('#version')).toHaveText('two', {timeout:30000});
  await o.ready(page);
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => Module.control?.().vblanks || 0), {timeout:90000}).toBeGreaterThan(600);
  expect(await page.evaluate(() => Module.frames.shown)).toBeGreaterThan(0);
  await expect(page.locator('#audio-warning')).toHaveText('');
  await expect(page.locator('#status')).not.toContainText(/error|abort/i);
  const updatedVblanks = await page.evaluate(() => Module.control().vblanks);
  // The diagnostic hash worker is deliberately outside the program cache.
  await context.setOffline(false); await site.start();
  const after = await hash(page); expect(after).toEqual(before);
  expect(failures).toEqual([]);
  h.evidence(browserName, 'offline-real', {before, after, vblanks, updatedVblanks});
});
