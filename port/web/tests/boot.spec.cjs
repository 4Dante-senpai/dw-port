const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const path = require('path');
for (const pattern of [false, true]) {
  test(pattern ? 'synthetic RGB15 display' : 'game frame zero', async ({page}, info) => {
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    page.on('console', message => {
      if (/abort|RuntimeError|signature mismatch/i.test(message.text())) errors.push(message.text());
    });
    const count = pattern ? 3 : 1;
    await page.goto(`/?validate=1&frames=${count}` + (pattern ? '&pattern=1' : ''));
    await expect(page.locator('#status')).toContainText(`reached frame ${count - 1}`);
    await expect(page.locator('#status')).toContainText('stack verified');
    await expect.poll(() => page.evaluate(() => Module.presentations.length)).toBe(count);
    const samples = await page.evaluate(() => Module.presentations);
    for (const [frame, sample] of samples.entries()) {
      expect(sample.frame).toBe(frame);
      expect(sample.different, `different=${sample.different}, first pixel=${sample.first}`).toBe(0);
      expect(sample.width).toBe(320); expect(sample.height).toBe(240);
      expect(sample.pixels.length).toBe(sample.width * sample.height * 4);
      expect(sample.ms).toBeGreaterThanOrEqual(0);
      if (pattern) {
        // Independent RGB15 oracle, reading GL's bottom-to-top rows.
        let different = 0, first = null;
        for (let y = 0; y < 240; y++) for (let x = 0; x < 320; x++) {
          const values = [(x + frame) & 31, (y + 2 * frame) & 31, (x + y + 3 * frame) & 31];
          const expected = values.map(v => (v << 3) | (v >> 2)).concat(255);
          const offset = ((239 - y) * 320 + x) * 4;
          const actual = sample.pixels.slice(offset, offset + 4);
          if (actual.some((v, i) => v !== expected[i])) {
            different++;
            first ||= {x, y, expected, actual};
          }
        }
        expect(different, `frame=${frame}, different=${different}, first=${JSON.stringify(first)}`).toBe(0);
      }
      delete sample.pixels;
    }
    expect(errors).toEqual([]);
    fs.writeFileSync(path.join(process.env.DW1_WEB_BUILD,
      `timing-${info.project.name}-${pattern ? 'pattern' : 'game'}.json`), JSON.stringify(samples));
  });
}
test('requires isolation before loading the runtime', async ({page}) => {
  const errors = [], runtime = [];
  page.on('pageerror', e => errors.push(String(e)));
  page.on('request', r => { if (/dw1\.(js|wasm)/.test(r.url())) runtime.push(r.url()); });
  await page.goto('/isolated-off/');
  await expect(page.locator('#status')).toHaveText('cross-origin isolation required');
  expect(await page.evaluate(() => typeof Module)).toBe('undefined');
  expect(runtime).toEqual([]); expect(errors).toEqual([]);
});

test('normal run keeps validation readback disabled', async ({page}) => {
  const errors = [];
  page.on('pageerror', e => errors.push(String(e)));
  await page.goto('/?frames=1&pattern=1');
  await expect(page.locator('#status')).toContainText('reached frame 0');
  expect(await page.evaluate(() => 'presentations' in Module)).toBe(false);
  const coordinates = [[1, 2], [31, 31], [100, 75], [319, 239]];
  const expected = coordinates.map(([x, y]) =>
    [x & 31, y & 31, (x + y) & 31].map(v => (v << 3) | (v >> 2)).concat(255));
  // Read the displayed placeholder canvas, without touching the worker's GL buffer.
  await expect.poll(() => page.evaluate(points => {
    const copy = document.createElement('canvas');
    copy.width = 320; copy.height = 240;
    const ctx = copy.getContext('2d');
    ctx.drawImage(document.querySelector('#canvas'), 0, 0);
    return points.map(([x, y]) => Array.from(ctx.getImageData(x, y, 1, 1).data));
  }, coordinates)).toEqual(expected);
  expect(errors).toEqual([]);
});
