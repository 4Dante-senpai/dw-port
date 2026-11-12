// L3: the same frame0 replay in both browsers and native, plus web recording roundtrip.
const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const {spawnSync} = require('child_process');
const fs = require('fs');
const path = require('path');
async function roundtrip(page, info, name, unfinished = false) {
  // Read and download in one main-thread task so periodic publication cannot race the click.
  const [download, state] = await Promise.all([page.waitForEvent('download'), page.evaluate(() => {
    const state = {snapshot: Module.playtestSnapshot, finished: Module.finished};
    document.querySelector('#record-inputs').click();
    return state;
  })]);
  const snapshot = state.snapshot;
  if (unfinished) expect(state.finished).not.toBe(0);
  const work = path.join(process.env.DW1_WEB_BUILD, `playtest-${info.project.name}-${name}`);
  fs.mkdirSync(work, {recursive: true});
  const recording = path.join(work, 'web.inputs.txt');
  await download.saveAs(recording);
  expect(fs.readFileSync(recording, 'utf8')).toBe(snapshot.inputs);
  const root = path.resolve(__dirname, '../../..');
  const result = spawnSync('python3', ['-I', '-B', path.join(root, 'tools/port/playtest/run.py'),
    '--build', 'native-headless', '--pack', path.join(process.env.DW1_WEB_BUILD, 'us.pack'),
    '--inputs', recording, '--vblanks', String(snapshot.vblank), '--output', path.join(work, 'native')],
    {cwd: root, encoding: 'utf8', timeout: 900000});
  expect(result.status, result.stderr + result.stdout).toBe(0);
  const report = JSON.parse(fs.readFileSync(path.join(work, 'native/report.json')));
  expect(report.trials[0].runs[0].digest).toBe(snapshot.digest);
}
test('live keyboard recording replays with the same digest in native', async ({page}, info) => {
  test.setTimeout(180000);
  const failures = errors(page);
  await seed(page, 'us.pack');
  await page.goto('/?pack=1&frames=0&vblanks=1200&record-inputs=1');
  await page.bringToFront();
  await page.locator('#screen').click();
  await expect.poll(() => page.evaluate(() => window.Module?.playtestSnapshot?.vblank || 0), {timeout: 60000}).toBeGreaterThanOrEqual(60);
  // Hold each key until the game thread has latched it: wall time per VBlank varies by machine.
  const recorded = () => page.evaluate(() => Module.playtestSnapshot.inputs);
  await page.keyboard.down('Enter');
  await expect.poll(recorded, {timeout: 60000}).toMatch(/ 0008\n/);
  await page.keyboard.up('Enter');
  await page.keyboard.down('k');
  await expect.poll(recorded, {timeout: 60000}).toMatch(/ 4000\n/);
  await page.keyboard.up('k');
  await roundtrip(page, info, 'live-partial', true);
  await expect.poll(() => page.evaluate(() => Module.finished), {timeout: 120000}).toBe(0);
  const snapshot = await page.evaluate(() => Module.playtestSnapshot);
  expect(snapshot.vblank).toBe(1200);
  expect(snapshot.inputs).toMatch(/0008/);
  expect(snapshot.inputs).toMatch(/4000/);
  expect(failures).toEqual([]);
  await roundtrip(page, info, 'live');
});

test('playtest digest and downloaded recording agree with native', async ({page}, info) => {
  test.setTimeout(1800000);
  const failures = errors(page);
  await seed(page, 'us.pack');
  const limit = Number(process.env.DW1_PLAYTEST_VBLANKS);
  await page.goto(`/?pack=1&frames=0&replay=playtest&vblanks=${limit}&digest=1&record-inputs=1&capture-mark=runMainMenu&capture-vblank=1000000`);
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout: 1200000}).toBe(0);
  const snapshot = await page.evaluate(() => Module.playtestSnapshot);
  expect(snapshot.vblank).toBe(limit);
  // Keep a private replay and the interval hashes even when the oracle fails.
  const evidence = path.join(process.env.DW1_WEB_BUILD, `playtest-${info.project.name}-oracle`);
  fs.mkdirSync(evidence, {recursive: true});
  fs.writeFileSync(path.join(evidence, 'inputs.txt'), snapshot.inputs);
  fs.writeFileSync(path.join(evidence, 'digest.txt'), await page.evaluate(() => FS.readFile('digest.txt', {encoding: 'utf8'})));
  const nativeRows = fs.readFileSync(path.join(process.env.DW1_WEB_BUILD,
    'playtest-native/trial-0000/build-0/digest.txt'), 'utf8').trim().split('\n');
  const webRows = fs.readFileSync(path.join(evidence, 'digest.txt'), 'utf8').trim().split('\n');
  let previous = 'start';
  let interval = 'none';
  for (let i = 0; i < Math.max(nativeRows.length, webRows.length); i++) {
    if (nativeRows[i] !== webRows[i]) {
      interval = `${previous}..${(nativeRows[i] || webRows[i]).split(' ')[0]}`;
      break;
    }
    previous = nativeRows[i].split(' ')[0];
  }
  expect(snapshot.digest, `first differing VBlank interval: ${interval}`).toBe(process.env.DW1_PLAYTEST_DIGEST);
  expect(failures).toEqual([]);
  await roundtrip(page, info, 'replay');
});

