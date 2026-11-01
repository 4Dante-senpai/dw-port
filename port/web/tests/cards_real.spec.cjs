const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const h = require('./cards_helpers.cjs');
const fs = require('node:fs');
const path = require('node:path');
const directory = process.env.DW1_CARD_DIR_WEB || '/tmp/dw1-web-cards';

async function ready(page) {
  await h.shell(page);
  await expect(page.locator('#instance')).toBeHidden();
  await expect(page.locator('#pack-status')).toHaveText(/^(Listo para jugar|Pack disponible)$/);
  await expect(page.locator('#status')).toHaveText('');
  await expect(page.locator('#play')).toBeEnabled();
}
async function replay(page, scene) {
  const fixture = JSON.parse(fs.readFileSync(path.join(directory, 'fixture.json')))[scene];
  expect(h.sha(fs.readFileSync(path.join(directory, scene + '.replay.txt')))).toBe(fixture.replay_sha256);
  await page.goto(`/?cards=1&pack=1&frames=0&replay=${scene}&capture-mark=${fixture.mark}&capture-vblank=${fixture.capture_vblank}`);
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:1000000}).toBe(0);
  expect(h.sha(Buffer.from(await page.evaluate(() => Module.capture)))).toBe(fixture.sha256);
  await page.evaluate(() => Module.cards.flush());
  expect(await page.evaluate(() => Module.storageAccesses)).toBe(0);
}
module.exports = {ready, replay, directory};
if (process.env.DW1_WEB_SUITE === 'cards_real') test('native card and frames match after save, closing, and importing', async ({page, context, browser}, info) => {
  test.setTimeout(3000000);
  const failures = errors(page);
  const native = fs.readFileSync(path.join(directory, 'card_newgame.mcd'));
  await seed(page, 'us.pack', fs.readFileSync(`${process.env.DW1_WEB_BUILD}/us.pack.sha256`, 'utf8').trim()); await ready(page);
  await expect(page.locator('#card-slot-1')).toContainText('Vacía');
  await replay(page, 'card_newgame');
  expect(await h.card(page)).toEqual(native);
  const stats = await page.evaluate(() => Module.cards.stats());
  expect(stats[0].persisted).toBe(stats[0].seq); expect(stats[0].failures).toBe(0);
  await page.close();
  const reopened = await context.newPage(); const moreFailures = errors(reopened);
  await ready(reopened);
  await expect(reopened.locator('#card-slot-1')).toContainText('Con datos');
  await replay(reopened, 'card_continue');
  const imported = await browser.newContext();
  try {
    const target = await imported.newPage(); const importFailures = errors(target);
    await seed(target, 'us.pack', fs.readFileSync(`${process.env.DW1_WEB_BUILD}/us.pack.sha256`, 'utf8').trim()); await ready(target);
    await h.importCard(target, native);
    await expect(target.locator('#card-slot-1')).toContainText('Con datos');
    expect(await h.exported(target)).toEqual(native);
    await replay(target, 'card_continue');
    expect(importFailures).toEqual([]);
  } finally {await imported.close();}
  expect(failures).toEqual([]); expect(moreFailures).toEqual([]);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/cards-real-${info.project.name}.json`, JSON.stringify({card_sha256:h.sha(native), stats, save_and_continue:true, import_and_continue:true}, null, 2));
});
