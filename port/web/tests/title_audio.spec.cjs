const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
test('the complete raw worklet PCM equals native title_audio byte for byte', async ({page}, info) => {
  test.setTimeout(1100000);
  const directory = process.env.DW1_PCM_DIR || '/tmp/dw1-web-title-audio';
  const fixture = JSON.parse(fs.readFileSync(path.join(directory, 'fixture.json')));
  const native = fs.readFileSync(path.join(directory, 'native.pcm'));
  const replay = fs.readFileSync(path.join(directory, 'replay.txt'));
  expect(crypto.createHash('sha256').update(native).digest('hex')).toBe(fixture.sha256);
  expect(crypto.createHash('sha256').update(replay).digest('hex')).toBe(fixture.inputs_sha256);
  const failures = errors(page);
  await seed(page, 'us.pack');
  // Sixteen extra VBlanks cover the complete 8192-frame ring, including calibrated latency.
  await page.goto(`/?pack=1&frames=0&audio=1&tap=1&replay=1&vblanks=${fixture.final_vblank + 16}`);
  await page.locator('#play').click();
  // Live input must be ignored throughout a resolved replay.
  await page.keyboard.down('Enter'); await page.keyboard.down('k');
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:1050000}).toBe(0);
  await page.waitForTimeout(100);
  // Small transfers keep the browser protocol from serializing the whole recording at once.
  const count = await page.evaluate(() => Module.audioTap.length);
  const chunks = [];
  for (let offset = 0; offset < count; offset += 256) {
    chunks.push(Buffer.from(await page.evaluate(([offset, count]) => {
      const parts = Module.audioTap.slice(offset, Math.min(count, offset + 256));
      const samples = new Int16Array(parts.reduce((sum, p) => sum + p.length, 0));
      let pos = 0; for (const part of parts) { samples.set(part, pos); pos += part.length; }
      const bytes = new Uint8Array(samples.buffer);
      let text = '';
      for (let i = 0; i < bytes.length; i += 16384)
        text += String.fromCharCode(...bytes.subarray(i, i + 16384));
      return btoa(text);
    }, [offset, count]), 'base64'));
  }
  const actual = Buffer.concat(chunks);
  let first = -1;
  for (let i = 0; i < Math.min(native.length, actual.length); i++) if (actual[i] !== native[i]) { first = i; break; }
  const result = {native_bytes: native.length, web_bytes: actual.length, first_difference_byte: first,
    first_difference_frame: first < 0 ? -1 : Math.floor(first / 4), sha256: fixture.sha256,
    ...await page.evaluate(() => Module.audioStats())};
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-audio-${info.project.name}.json`, JSON.stringify(result, null, 2));
  expect(actual.length, 'web PCM must cover the entire native recording').toBeGreaterThanOrEqual(native.length);
  expect(first, `first PCM difference at byte ${first}, stereo frame ${Math.floor(first / 4)}`).toBe(-1);
  // This is raw PCM fidelity; the ten-minute L4 test gates uninterrupted playback.
  expect(result.overflows).toBe(0);
  expect(failures).toEqual([]);
});
