const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const h = require('./cards_helpers.cjs');
const {ready, replay, directory} = require('./cards_real.spec.cjs');
const {execFileSync} = require('node:child_process');
const path = require('node:path');
const fs = require('node:fs');
test('Redux card continues in web, and Redux loads a card saved and exported by web', async ({page, browser}, info) => {
  test.setTimeout(16000000);
  const failures = errors(page);
  await seed(page, 'us.pack', fs.readFileSync(`${process.env.DW1_WEB_BUILD}/us.pack.sha256`, 'utf8').trim()); await ready(page);
  const redux = fs.readFileSync(path.join(directory, 'redux-newgame.mcd'));
  await h.importCard(page, redux);
  await expect(page.locator('#card-slot-1')).toContainText('Con datos');
  await replay(page, 'card_continue');
  // The export must come from a web save: exporting the imported card would only round-trip Redux's bytes.
  const saved = await browser.newContext();
  const target = await saved.newPage(); const savedFailures = errors(target);
  await seed(target, 'us.pack', fs.readFileSync(`${process.env.DW1_WEB_BUILD}/us.pack.sha256`, 'utf8').trim()); await ready(target);
  await replay(target, 'card_newgame');
  await ready(target);
  const bytes = await h.exported(target);
  await saved.close();
  expect(savedFailures).toEqual([]);
  const file = path.join(directory, `web-export-${info.project.name}.mcd`);
  fs.writeFileSync(file, bytes);
  execFileSync('python3', ['-I', '-B', path.resolve(__dirname, '../../../tools/port/scenes/cards_redux.py'),
    'verify', '--card', file, '--browser', info.project.name], {timeout:15000000, stdio:'inherit'});
  expect(failures).toEqual([]);
});
