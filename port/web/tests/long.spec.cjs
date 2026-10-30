// L4 for the frame queue (not in ctest, run by hand): run.py <build> long. Every frame reaches the
// main thread as an ImageBitmap without backpressure, so a slower main thread would show up here
// as a growing delay and growing browser memory.
const {test, expect} = require('/opt/node_modules/@playwright/test');
const {execSync} = require('child_process');
const fs = require('fs');
const {seed, errors} = require('./storage.cjs');
const RATE = 60.0087172527;
const MINUTES = Number(process.env.DW1_WEB_LONG_MINUTES || 10);
// Resident memory of the browser's processes, in MiB.
const browserMemory = () => execSync('ps -eo rss=,comm=').toString().split('\n')
  .filter(line => /chrome|firefox|Web Content|Isolated|WebExtensions|Socket Process|RDD|Privileged/.test(line))
  .reduce((sum, line) => sum + Number(line.trim().split(/\s+/)[0]), 0) / 1024;
test('a long run keeps the frame queue empty', async ({page}, info) => {
  test.setTimeout((MINUTES + 3) * 60000);
  const failures = errors(page);
  await seed(page, 'us.pack');
  await page.goto('/?pack=1&frames=0');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 60000}).toBeGreaterThan(120);
  const sample = () => page.evaluate(() => {
    const result = {t: performance.now(), ...Module.control(), shown: Module.frames.shown, latency: Module.frames.latency};
    Module.frames.latency = 0;
    return result;
  });
  const first = await sample(), samples = [];
  while (samples.length < MINUTES * 6) {
    await page.waitForTimeout(10000);
    samples.push({...await sample(), rss: browserMemory()});
  }
  // A blocked main thread queues frames; they must drain once it is free.
  await page.evaluate(() => { for (const end = performance.now() + 2000; performance.now() < end;); });
  await page.waitForTimeout(1000);
  const blocked = await sample();
  await page.waitForTimeout(5000);
  const drained = await sample();
  const last = samples.at(-1), minute = Math.ceil(samples.length / MINUTES);
  const mean = list => list.reduce((sum, s) => sum + s.rss, 0) / list.length;
  const result = {
    project: info.project.name, seconds: (last.t - first.t) / 1000,
    vblanks: last.vblanks - first.vblanks,
    expected_vblanks: (last.t - first.t) / 1000 * RATE,
    shown: last.shown - first.shown,
    resynchronizations: last.resynchronizations,
    max_latency_ms: Math.max(...samples.map(s => s.latency)),
    rss_second_minute_mib: mean(samples.slice(minute, 2 * minute)),
    rss_last_minute_mib: mean(samples.slice(-minute)),
    blocked_latency_ms: blocked.latency, drained_latency_ms: drained.latency, samples
  };
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/long-${info.project.name}.json`, JSON.stringify(result, null, 2));
  expect(failures).toEqual([]);
  expect(Math.abs(result.vblanks - result.expected_vblanks) / result.expected_vblanks).toBeLessThan(0.001);
  expect(result.resynchronizations).toBe(0);
  // Under four frames of delay, at most one queued behind the one being shown.
  expect(result.max_latency_ms).toBeLessThan(67);
  expect(result.rss_last_minute_mib / result.rss_second_minute_mib).toBeLessThan(1.1);
  expect(result.blocked_latency_ms).toBeGreaterThan(1000);
  expect(result.drained_latency_ms).toBeLessThan(67);
});
