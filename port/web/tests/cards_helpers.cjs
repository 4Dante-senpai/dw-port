const {expect} = require('/opt/node_modules/@playwright/test');
const fs = require('node:fs');
const crypto = require('node:crypto');
const size = 131072;
const pattern = n => Buffer.from(Array.from({length:size}, (_, i) => (n + i * 7) & 255));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
async function card(page, port = 0, value) {
  const result = await page.evaluate(({port, value}) => new Promise((resolve, reject) => {
    const open = indexedDB.open('dw1');
    open.onerror = () => reject(open.error);
    open.onsuccess = () => {
      const db = open.result;
      const tx = db.transaction('cards', value === undefined ? 'readonly' : 'readwrite');
      const request = value === undefined ? tx.objectStore('cards').get(port)
        : tx.objectStore('cards').put(Uint8Array.from(value).buffer, port);
      tx.oncomplete = () => {
        db.close();
        resolve(value === undefined && request.result instanceof ArrayBuffer ? Array.from(new Uint8Array(request.result)) : null);
      };
      tx.onabort = tx.onerror = () => { db.close(); reject(tx.error || Error('aborted')); };
    };
  }), {port, value: value === undefined ? undefined : Array.from(value)});
  return result === null ? null : Buffer.from(result);
}
async function shell(page, url = '/') {
  await page.goto(url);
  await expect(page.locator('#instance')).toBeHidden();
  // Not a click: hiding #welcome moves the summary, and Firefox's click can land off it.
  await page.locator('#cards').evaluate(details => { details.open = true; });
  await expect(page.locator('#card-slot-1')).toBeVisible();
}
async function finished(page) {
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:30000}).toBe(0);
}
async function persisted(page, seq) {
  await expect.poll(() => page.evaluate(() => Module.cards.stats()[0].persisted)).toBe(seq);
}
async function exported(page, port = 1) {
  const download = page.waitForEvent('download');
  await page.locator(`#card-slot-${port} button`).click();
  const file = await download;
  expect(file.suggestedFilename()).toBe(`digimon-world-card-${port}.mcd`);
  return fs.readFileSync(await file.path());
}
async function importCard(page, bytes, port = 1, name = 'save.mcd') {
  await page.locator(`#card-slot-${port} input`).setInputFiles({name, mimeType:'application/octet-stream', buffer:bytes});
}
module.exports = {size, pattern, sha, card, shell, finished, persisted, exported, importCard};
