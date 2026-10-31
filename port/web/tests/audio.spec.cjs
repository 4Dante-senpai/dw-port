const {test, expect} = require('/opt/node_modules/@playwright/test');
const {store, errors} = require('./storage.cjs');
const fs = require('node:fs');
const stats = page => page.evaluate(() => Module.audioStats());
async function running(page, args = '') {
  await page.goto('/?frames=0&audio=1&audio-ramp=1&tap=1' + args);
  await expect(page.locator('#play')).toBeEnabled();
  expect(await page.evaluate(() => window.Module)).toBeUndefined();
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => window.Module?.audioContext?.state)).toBe('running');
  await expect.poll(() => page.evaluate(() => window.Module?.audioStats?.().written || 0), {timeout:60000}).toBeGreaterThan(10000);
}
async function volume(page, value, change = true) {
  await page.locator('#volume').evaluate((slider, {value, change}) => {
    slider.focus(); slider.value = value; slider.dispatchEvent(new Event('input'));
    if (change) slider.dispatchEvent(new Event('change'));
  }, {value, change});
}
test('ramp is ordered, live gain halves output, mute keeps consuming, pause and hidden preserve counters', async ({page}, info) => {
  test.setTimeout(240000);
  const failures = errors(page), stages = [];
  await running(page);
  stages.push({stage: "started", ...await stats(page)});
  await page.evaluate(() => {
    const context = Module.audioContext, merger = context.createChannelMerger(2), silent = context.createGain();
    Module.afterGain = context.createAnalyser();
    Module.afterGain.fftSize = 2048;
    Module.audioGain.connect(Module.afterGain);
    // Both signals must come from one render window: the ramp's RMS moves up to 2.6x between
    // windows, so two analyser reads a few quanta apart do not measure the gain.
    Module.gainProbe = context.createScriptProcessor(2048, 2, 1);
    const rms = samples => Math.sqrt(samples.reduce((sum, x) => sum + x*x, 0) / samples.length);
    Module.gainProbe.onaudioprocess = e => {
      Module.gainRatio = rms(e.inputBuffer.getChannelData(1)) / rms(e.inputBuffer.getChannelData(0));
    };
    silent.gain.value = 0;
    Module.audioNode.connect(merger, 0, 0); Module.audioGain.connect(merger, 0, 1);
    merger.connect(Module.gainProbe); Module.gainProbe.connect(silent); silent.connect(context.destination);
  });
  await volume(page, '50', false);
  expect(await page.evaluate(() => Module.audioGain.gain.value)).toBe(0.5);
  expect(await store(page)).toBeUndefined();
  await expect.poll(() => page.evaluate(() => Module.gainRatio), {timeout:10000}).toBeCloseTo(0.5, 3);
  const ratio = await page.evaluate(() => { Module.gainProbe.disconnect(); return Module.gainRatio; });
  expect(await page.locator('#volume').evaluate(e => e === document.activeElement)).toBe(true);
  await volume(page, '0');
  await page.waitForTimeout(250);
  expect(await page.evaluate(() => {
    const samples = new Float32Array(2048); Module.afterGain.getFloatTimeDomainData(samples);
    return samples.every(v => v === 0);
  })).toBe(true);
  const muted = await stats(page);
  await page.waitForTimeout(1000);
  expect((await stats(page)).written).toBeGreaterThan(muted.written);
  expect((await stats(page)).underruns).toBe(0);
  stages.push({stage: "before_pause", ...await stats(page)});
  await expect.poll(() => page.evaluate(() => Module.control().vblanks), {timeout:30000}).toBeGreaterThan(60);
  await page.locator('#pause').click();
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  const paused = await stats(page);
  await page.waitForTimeout(30000);
  expect((await stats(page)).underruns).toBe(paused.underruns);
  await page.locator('#pause').click();
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('running');
  await page.waitForTimeout(1000);
  await page.evaluate(() => {
    Object.defineProperty(document, 'visibilityState', {configurable: true, value: 'hidden'});
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  const hidden = await stats(page);
  stages.push({stage: 'hidden', ...hidden});
  await page.waitForTimeout(1000);
  expect((await stats(page)).underruns).toBe(hidden.underruns);
  await page.evaluate(() => {
    Object.defineProperty(document, 'visibilityState', {configurable: true, value: 'visible'});
    document.dispatchEvent(new Event('visibilitychange'));
  });
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('running');
  await page.waitForTimeout(1000);
  // Freeze the stream before transporting a large recording through the test protocol.
  expect(await page.evaluate(() => Module.control().resynchronizations)).toBe(0);
  await page.locator('#pause').click();
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  await page.waitForTimeout(100);
  const raw = await page.evaluate(() => Module.audioTap.flatMap(chunk => Array.from(chunk)));
  expect(raw.length).toBeGreaterThan(20000);
  const result = await stats(page);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/audio-${info.project.name}.json`, JSON.stringify({ratio, ...result, raw_frames: raw.length/2, stages}));
  for (let i = 0; i < raw.length / 2; i++) {
    const s16 = x => (x << 16) >> 16;
    if (raw[2*i] !== s16(i*31) || raw[2*i+1] !== s16(i*17+123))
      throw Error(`raw ramp differs at frame ${i}: ${raw[2*i]},${raw[2*i+1]}`);
  }
  expect(result.underruns).toBe(0); expect(result.overflows).toBe(0);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/audio-${info.project.name}.json`, JSON.stringify({ratio, ...result, raw_frames: raw.length/2, stages}));
  expect(failures).toEqual([]);
});
test('v2 migration preserves extras; volume persists; restore only restores controls', async ({page}) => {
  await page.goto('/');
  await expect(page.locator('#launcher')).toBeVisible();
  // Obtain the defaults through a controls edit, then seed the previous schema with extras.
  await page.locator('#options summary').click();
  await page.locator('#restore-defaults').click();
  await expect.poll(() => store(page)).toBeTruthy();
  const previous = await store(page);
  delete previous.volume; previous.schema_version = 2; previous.extra = {kept: [1, 2]};
  previous.controls.extra = 'kept';
  await store(page, previous); await page.reload();
  await expect(page.locator('#launcher')).toBeVisible();
  const migrated = await store(page);
  expect(migrated).toEqual({...previous, schema_version: 3, volume: 1});
  await page.locator('#options summary').click();
  await volume(page, '30');
  await expect.poll(async () => (await store(page)).volume).toBe(0.3);
  await page.locator('#restore-defaults').click();
  await expect.poll(async () => (await store(page)).volume).toBe(0.3);
  await page.reload();
  await expect(page.locator('#volume')).toHaveValue('30');
  await running(page);
  expect(await page.evaluate(() => Module.audioGain.gain.value)).toBeCloseTo(0.3);
});
for (const value of [-0.1, 1.1, '0.3', null]) test(`invalid volume ${JSON.stringify(value)} protects the record`, async ({page}) => {
  await page.goto('/'); await expect(page.locator('#launcher')).toBeVisible();
  await page.locator('#options summary').click(); await page.locator('#restore-defaults').click();
  await expect.poll(() => store(page)).toBeTruthy();
  const record = {...await store(page), volume: value};
  await store(page, record); await page.reload();
  await expect(page.locator('#settings-warning')).toContainText('no se pueden leer');
  await page.locator('#options summary').click(); await volume(page, '40');
  expect(await store(page)).toEqual(record);
});
for (const failure of ['constructor', 'addModule']) test(`audio ${failure} failure runs silently with a warning`, async ({page}) => {
  const failures = errors(page);
  await page.addInitScript(failure => {
    if (failure === 'constructor') window.AudioContext = class { constructor() { throw Error('test audio unavailable'); } };
    else AudioWorklet.prototype.addModule = () => Promise.reject(Error('test worklet unavailable'));
  }, failure);
  await page.goto('/?frames=0&audio=1&audio-ramp=1'); await page.locator('#play').click();
  await expect(page.locator('#audio-warning')).toContainText('Audio no disponible');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks || 0), {timeout:30000}).toBeGreaterThan(60);
  expect((await stats(page)).written).toBe(0);
  expect(failures).toEqual([]);
});
test('without an AudioContext the existing query boot never writes PCM', async ({page}) => {
  await page.goto('/?frames=0');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks || 0), {timeout:30000}).toBeGreaterThan(60);
  expect((await stats(page)).written).toBe(0);
});
test('a suspended device has bounded backpressure and no underruns', async ({page}) => {
  await running(page);
  await expect.poll(() => page.evaluate(() => Module.control().vblanks), {timeout:30000}).toBeGreaterThan(60);
  // Lose an already active device without publishing a game pause. The producer
  // must exhaust its bounded wait and keep the game alive without a consumer.
  await page.evaluate(() => Module.audioContext.suspend());
  const before = await stats(page);
  await expect.poll(async () => (await stats(page)).overflows, {timeout:10000}).toBeGreaterThan(before.overflows);
  for (let second = 0; second < 3; second++) {
    const vblank = await page.evaluate(() => Module.control().vblanks);
    await page.waitForTimeout(1000);
    expect(await page.evaluate(() => Module.control().vblanks) - vblank).toBeGreaterThanOrEqual(45);
  }
  expect(await page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  const result = await stats(page);
  expect(result.level).toBeLessThanOrEqual(8192); expect(result.underruns).toBe(0);
  expect(await page.evaluate(() => Module.finished)).toBeUndefined();
});
test('a device that never resumes warns and finishes without producing PCM', async ({page}) => {
  await page.addInitScript(() => { AudioContext.prototype.resume = function() { return this.suspend(); }; });
  await page.goto('/?frames=1&audio=1&audio-ramp=1');
  await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:30000}).toBe(0);
  await expect(page.locator('#audio-warning')).toContainText('Audio no disponible');
  expect(await page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  const result = await stats(page);
  expect(result.written).toBe(0); expect(result.level).toBe(0);
  expect(result.underruns).toBe(0); expect(result.overflows).toBe(0);
  expect(await page.evaluate(() => Module.audioNode)).toBeNull();
});
for (const failure of ['resume', 'addModule']) test(`pending ${failure} at startup warns and advances VBlanks`, async ({page}) => {
  await page.addInitScript(failure => {
    if (failure === 'resume') {
      const resume = AudioContext.prototype.resume;
      AudioContext.prototype.resume = function() {
        resume.call(this).catch(() => {});
        return new Promise(() => {});
      };
    } else AudioWorklet.prototype.addModule = () => new Promise(() => {});
  }, failure);
  await page.goto('/?frames=0&audio=1&audio-ramp=1');
  await page.locator('#play').click();
  await expect(page.locator('#audio-warning')).toContainText('Audio no disponible', {timeout:12000});
  await expect.poll(() => page.evaluate(() => Module.control?.().vblanks || 0), {timeout:30000}).toBeGreaterThan(60);
  expect(await page.evaluate(() => Module.control().pause)).toBe(0);
  if (failure === 'addModule') expect(await page.evaluate(() => Module.audioNode)).toBeNull();
});
test('pending resume on unpause warns and advances VBlanks', async ({page}) => {
  await running(page);
  await expect.poll(() => page.evaluate(() => Module.control().vblanks), {timeout:30000}).toBeGreaterThan(60);
  await page.locator('#pause').click();
  await expect.poll(() => page.evaluate(() => Module.control().held)).toBe(1);
  await page.evaluate(() => { Module.audioContext.resume = () => new Promise(() => {}); });
  const vblank = await page.evaluate(() => Module.control().vblanks);
  await page.locator('#pause').click();
  await expect(page.locator('#audio-warning')).toContainText('Audio no disponible', {timeout:7000});
  await expect.poll(() => page.evaluate(() => Module.control().vblanks), {timeout:10000}).toBeGreaterThan(vblank + 60);
});
test('volume keeps keyboard focus for consecutive arrow keys and blurs on pointer release', async ({page}) => {
  await running(page);
  const slider = page.locator('#volume');
  await slider.focus();
  await page.keyboard.press('ArrowLeft');
  await page.keyboard.press('ArrowLeft');
  await expect(slider).toHaveValue('98');
  await expect(slider).toBeFocused();
  expect(await page.evaluate(() => Module.audioGain.gain.value)).toBeCloseTo(0.98);
  await slider.dispatchEvent('pointerup');
  await expect(slider).not.toBeFocused();
});
test('finishing suspends the context before any underrun', async ({page}) => {
  await page.goto('/?frames=1&audio=1&audio-ramp=1'); await page.locator('#play').click();
  await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:60000}).toBe(0);
  await expect.poll(() => page.evaluate(() => Module.audioContext.state)).toBe('suspended');
  const finished = await stats(page); await page.waitForTimeout(500);
  expect((await stats(page)).underruns).toBe(finished.underruns);
});
