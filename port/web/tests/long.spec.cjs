// L4 for frame presentation (not in ctest, run by hand): run.py <build> long. The main thread paints
// the newest frame at each animation frame, so a slow main thread shows up as frames not painted
// and frames that are never freed as growing browser memory.
const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const {seed, errors, browserMemory} = require('./storage.cjs');
const RATE = 60.0087172527;
const MINUTES = Number(process.env.DW1_WEB_LONG_MINUTES || 10);
test('a long run paints every frame without growing', async ({page}, info) => {
  test.setTimeout((MINUTES + 3) * 60000);
  const failures = errors(page);
  page.on('console', message => {
    if (message.text().includes('pacing resynchronization')) console.log(message.text());
  });
  await seed(page, 'us.pack');
  await page.goto('/?pack=1&frames=0&audio=1');
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => window.Module?.audioContext?.state)).toBe('running');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 60000}).toBeGreaterThan(120);
  const sample = () => page.evaluate(() => ({t: performance.now(), ...Module.control(), ...Module.frames, ...Module.audioStats()}));
  const first = await sample(), samples = [];
  while (samples.length < MINUTES * 6) {
    await page.waitForTimeout(10000);
    samples.push({...await sample(), rss: browserMemory()});
    if (samples.length % 6 === 0) console.log(JSON.stringify({minute: samples.length / 6, ...samples.at(-1)}));
  }
  // The game must survive a blocked main thread; Firefox's glReadPixels holds it meanwhile.
  const vblanksBlocked = await page.evaluate(() => {
    const start = Module.control().vblanks;
    for (const end = performance.now() + 2000; performance.now() < end;);
    return Module.control().vblanks - start;
  });
  const blocked = await sample();
  await page.waitForTimeout(5000);
  const after = await sample();
  const last = samples.at(-1), minute = Math.ceil(samples.length / MINUTES);
  const mean = list => list.reduce((sum, s) => sum + s.rss, 0) / list.length;
  const result = {
    project: info.project.name, seconds: (last.t - first.t) / 1000,
    vblanks: last.vblanks - first.vblanks,
    expected_vblanks: (last.t - first.t) / 1000 * RATE,
    written: last.written - first.written,
    shown: last.shown - first.shown,
    presented: last.presented - first.presented,
    resynchronizations: last.resynchronizations,
    underruns: last.underruns, overflows: last.overflows,
    after_block_underruns: after.underruns, after_block_overflows: after.overflows,
    rss_second_minute_mib: mean(samples.slice(minute, 2 * minute)),
    rss_last_minute_mib: mean(samples.slice(-minute)),
    vblanks_while_blocked: vblanksBlocked, shown_after_block: after.shown - blocked.shown, samples
  };
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/long-${info.project.name}.json`, JSON.stringify(result, null, 2));
  expect(failures).toEqual([]);
  expect(result.underruns).toBe(0); expect(result.overflows).toBe(0);
  expect(result.written / (44100 * result.seconds)).toBeGreaterThanOrEqual(0.99);
  expect(Math.abs(result.vblanks - result.expected_vblanks) / result.expected_vblanks).toBeLessThan(0.001);
  expect(result.resynchronizations).toBe(0);
  expect(result.shown / result.presented).toBeGreaterThan(0.99);
  expect(result.rss_last_minute_mib / result.rss_second_minute_mib).toBeLessThan(1.1);
  expect(result.shown_after_block).toBeGreaterThan(100);
});
