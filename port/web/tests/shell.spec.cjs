const {test, expect} = require('/opt/node_modules/@playwright/test');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const {errors} = require('./storage.cjs');
const RATE = 60.0087172527;
// Requests that would start the game or touch OPFS through a worker.
function runtime(page) {
  const urls = [];
  page.on('request', r => { if (/\/(dw1\.(js|wasm)|io_worker\.js|seed_worker\.js)/.test(r.url())) urls.push(r.url()); });
  page.on('worker', w => urls.push(w.url()));
  return urls;
}
async function missing(page, url, expected) {
  const failures = errors(page), started = runtime(page);
  await page.goto(url);
  await expect(page.locator('#capabilities')).toBeVisible();
  const names = await page.locator('#capabilities li').evaluateAll(items => items.map(i => i.dataset.capability));
  expect(names).toEqual(expected);
  await expect(page.locator('#status')).toHaveText('El juego no puede arrancar en este navegador.');
  await expect(page.locator('#launcher')).toBeHidden();
  await page.waitForTimeout(500);
  expect(await page.evaluate(() => typeof Module)).toBe('undefined');
  expect(started).toEqual([]); expect(failures).toEqual([]);
}
test('without cross-origin isolation the Shell names it and loads nothing', async ({page}) => {
  // Browsers also withhold SharedArrayBuffer without isolation.
  await missing(page, '/isolated-off/', ['isolation', 'shared-memory']);
});
test('OPFS that refuses access counts as missing', async ({page}) => {
  await page.addInitScript(() => {
    StorageManager.prototype.getDirectory = () => Promise.reject(new DOMException('denied', 'SecurityError'));
  });
  await missing(page, '/', ['storage']);
});
test('without SharedArrayBuffer the Shell names it and loads nothing', async ({page}) => {
  await page.addInitScript(() => { delete globalThis.SharedArrayBuffer; });
  await missing(page, '/', ['shared-memory']);
});
test('without OPFS the Shell names it and loads nothing', async ({page}) => {
  await page.addInitScript(() => { delete StorageManager.prototype.getDirectory; });
  await missing(page, '/', ['storage']);
});
test('without Web Locks the Shell names it and loads nothing', async ({page}) => {
  await page.addInitScript(() => { delete Navigator.prototype.locks; });
  await missing(page, '/', ['locks']);
});
test('a second tab waits for the dw1-game lock without starting anything', async ({page, context}) => {
  const failures = errors(page);
  await page.goto('/');
  await expect(page.locator('#launcher')).toBeVisible();
  await expect(page.locator('#welcome')).toBeVisible();
  await expect(page.locator('#legal')).toContainText('No se distribuye ningún dato del juego');
  await expect(page.locator('#welcome')).toContainText('SLUS-01032');
  for (const id of ['#drop-zone', '#play', '#options', '#cards', '#clear']) await expect(page.locator(id)).toBeVisible();
  const second = await context.newPage();
  const secondFailures = errors(second), started = runtime(second);
  await second.goto('/');
  await expect(second.locator('#instance')).toBeVisible();
  await expect(second.locator('#instance')).toContainText('otra pestaña');
  await expect(second.locator('#launcher')).toBeHidden();
  await second.waitForTimeout(1000);
  expect(await second.evaluate(() => typeof Module)).toBe('undefined');
  expect(started).toEqual([]);
  await page.close();
  await expect(second.locator('#instance')).toBeHidden();
  await expect(second.locator('#launcher')).toBeVisible();
  await expect(second.locator('#pack-status')).toHaveText('Pack ausente');
  expect(failures).toEqual([]); expect(secondFailures).toEqual([]);
});

const sample = page => page.evaluate(() => ({t: performance.now(), ...Module.control()}));
async function running(page) {
  await page.goto('/?frames=1000000');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout:30000}).toBeGreaterThan(60);
  await expect(page.locator('#pause')).toHaveText('Pausa');
}
// VBlanks per second of wall time between two samples; frames end on whole VBlanks.
function rate(a, b) { return (b.vblanks - a.vblanks) * 1000 / (b.t - a.t); }
async function expectPaced(page, label, result) {
  const a = await sample(page);
  await page.waitForTimeout(2000);
  const b = await sample(page);
  result[label] = rate(a, b);
  expect(Math.abs(result[label] - RATE) / RATE, `${label}: ${result[label]} VBlank/s`).toBeLessThan(0.05);
}
async function expectHeld(page, result, label) {
  await expect.poll(() => page.evaluate(() => Module.control().held)).toBe(1);
  const a = await sample(page);
  await page.waitForTimeout(2000);
  const b = await sample(page);
  expect(b.vblanks).toBe(a.vblanks);
  expect(b.held).toBe(1);
  result[label] = {vblanks: a.vblanks, ms: b.t - a.t};
}
function hide(page, hidden) {
  return page.evaluate(hidden => {
    Object.defineProperty(document, 'visibilityState', {configurable: true, get: () => hidden ? 'hidden' : 'visible'});
    Object.defineProperty(document, 'hidden', {configurable: true, get: () => hidden});
    document.dispatchEvent(new Event('visibilitychange'));
  }, hidden);
}
function evidence(info, name, data) {
  fs.writeFileSync(path.join(process.env.DW1_WEB_BUILD, `shell-${name}-${info.project.name}.json`), JSON.stringify(data, null, 2));
}
test('Pausa freezes VBlanks and resumes without owing the paused wall time', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page), result = {};
  await running(page);
  await expectPaced(page, 'rate_before', result);
  await page.locator('#pause').click();
  await expect(page.locator('#pause')).toHaveText('Reanudar');
  await expectHeld(page, result, 'paused');
  await page.locator('#pause').click();
  await expect(page.locator('#pause')).toHaveText('Pausa');
  await expect.poll(() => page.evaluate(() => Module.control().held)).toBe(0);
  await expectPaced(page, 'rate_after', result);
  const end = await sample(page);
  expect(end.resynchronizations).toBe(0);
  expect(end.pauses).toBe(1);
  expect(failures).toEqual([]);
  evidence(info, 'pause', {...result, end});
});
test('a hidden tab pauses; showing it resumes unless Pausa is active', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page), result = {};
  await running(page);
  await hide(page, true);
  await expectHeld(page, result, 'hidden');
  await hide(page, false);
  await expect.poll(() => page.evaluate(() => Module.control().held)).toBe(0);
  await expectPaced(page, 'rate_after_visible', result);
  await page.locator('#pause').click();
  await hide(page, true);
  await hide(page, false);
  await expectHeld(page, result, 'manual_after_visible');
  await page.locator('#pause').click();
  await expectPaced(page, 'rate_after_manual', result);
  const end = await sample(page);
  expect(end.resynchronizations).toBe(0);
  expect(end.pauses).toBe(2);
  expect(failures).toEqual([]);
  evidence(info, 'visibility', {...result, end});
});
test('a frame later than 4 VBlanks resynchronizes once, without a catch-up burst', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page), result = {};
  await running(page);
  await page.evaluate(() => Module.stall(500));
  await expect.poll(() => page.evaluate(() => Module.control().resynchronizations)).toBe(1);
  await expectPaced(page, 'rate_after_stall', result);
  const end = await sample(page);
  expect(end.resynchronizations).toBe(1);
  expect(end.pauses).toBe(0);
  expect(failures).toEqual([]);
  evidence(info, 'stall', {...result, end});
});
async function seedSynthetic(page) {
  const pack = fs.readFileSync(path.join(process.env.DW1_WEB_BUILD, 'synthetic.pack'));
  const sha = crypto.createHash('sha256').update(pack).digest('hex');
  await page.route('**/__fixture/us.pack.sha256', r => r.fulfill({status: 404, body: ''}));
  await page.goto('/seed.html');
  await page.evaluate(sha => new Promise((resolve, reject) => {
    const worker = new Worker('/seed_worker.js');
    worker.onmessage = ({data}) => {
      if (data.step || data.progress !== undefined) return;
      worker.terminate(); data.error ? reject(Error(data.error)) : resolve();
    };
    worker.postMessage({url: '/__fixture/synthetic.pack', sha256: sha});
  }), sha);
}
// Another worker keeps us.pack locked, as a page that ran the game does while it tears down.
function holdPack(page) {
  return page.evaluate(() => new Promise((resolve, reject) => {
    const code = `(async () => {
      const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
      await (await dir.getFileHandle('us.pack')).createSyncAccessHandle();
      postMessage('held');
    })().catch(e => postMessage(e.name));`;
    window.holder = new Worker(URL.createObjectURL(new Blob([code], {type: 'text/javascript'})));
    holder.onmessage = ({data}) => data === 'held' ? resolve() : reject(Error(data));
  }));
}
test('Jugar is the only gesture: game on this page with a running AudioContext', async ({page}, info) => {
  const failures = errors(page);
  await seedSynthetic(page);
  // Returning with a Pack offers "Jugar" directly, without the welcome.
  await page.goto('/');
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  await expect(page.locator('#launcher')).toBeVisible();
  await expect(page.locator('#welcome')).toBeHidden();
  await expect(page.locator('#play')).toBeEnabled();
  await expect(page.locator('#disc')).toBeEnabled();
  // Playwright's evaluations grant user activation, so nothing touches the Shell before the click:
  // an init script records the transient activation at each AudioContext construction, and a probe
  // context made at load without a gesture is the negative control (autoplay blocked).
  await page.addInitScript(() => {
    const Native = AudioContext;
    window.audioLog = [];
    window.AudioContext = class extends Native {
      constructor(...args) { super(...args); audioLog.push(navigator.userActivation.isActive); }
    };
    addEventListener('load', () => {
      const probe = new Native();
      setTimeout(() => { window.probe = {state: probe.state, activated: navigator.userActivation.hasBeenActive}; probe.close(); }, 300);
    });
  });
  await page.goto('/');
  await page.waitForTimeout(1500);
  const url = page.url();
  await page.locator('#play').click();
  const probe = await page.evaluate(() => window.probe);
  expect(probe).toEqual({state: 'suspended', activated: false});
  expect(await page.evaluate(() => window.audioLog)).toEqual([true]);
  await expect(page.locator('#launcher')).toBeHidden();
  await expect(page.locator('#game')).toBeVisible();
  await expect.poll(() => page.evaluate(() => Module.audioContext?.state)).toBe('running');
  expect(page.url()).toBe(url);
  // callMain prepends the program name to Module.arguments when main starts.
  expect((await page.evaluate(() => Module.arguments)).slice(-4)).toEqual(['--frames', '0', '--pack', 'dw1/us.pack']);
  // The synthetic Pack reaches the game's C (via the I/O worker), which rejects its Manifest;
  // reaching the title is the class B Escena (title.spec.cjs).
  await expect.poll(() => page.evaluate(() => Module.finished)).toBe(1);
  await expect(page.locator('#status')).toContainText('Manifest');
  await expect(page.locator('#pause')).toBeHidden();
  // "Borrar datos" brings back the welcome and the own-copy notice.
  await page.goto('/');
  await page.locator('#clear').click();
  await expect.poll(() => page.evaluate(() => [document.querySelector('#pack-status').textContent, document.querySelector('#status').textContent])).toEqual(['Pack ausente', '']);
  await expect(page.locator('#welcome')).toBeVisible();
  await expect(page.locator('#legal')).toBeVisible();
  expect(failures).toEqual([]);
  evidence(info, 'play', {probe_without_gesture: probe, audio_context: 'running', navigations: 0});
});
test('Borrar datos waits for a held Pack and reports a handle that stays held', async ({page}) => {
  test.setTimeout(120000);
  await seedSynthetic(page);
  await page.goto('/');
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  await holdPack(page);
  await page.locator('#clear').click();
  await page.waitForTimeout(1000);
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  await page.evaluate(() => holder.terminate());
  await expect(page.locator('#pack-status')).toHaveText('Pack ausente', {timeout: 10000});
  await seedSynthetic(page);
  await page.goto('/');
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
  await holdPack(page);
  await page.locator('#clear').click();
  await expect(page.locator('#status')).not.toHaveText('', {timeout: 10000});
  await expect(page.locator('#pack-status')).toHaveText('Listo para jugar');
});
test('a failed game download says so', async ({page}) => {
  await page.route('**/dw1.js*', r => r.abort());
  await page.goto('/?frames=1');
  await expect(page.locator('#status')).toHaveText('No se pudo descargar el juego. Recargá la página para reintentar.');
});
test('frames reach the screen while the game thread runs', async ({page}) => {
  await page.goto('/?frames=100000&pattern=1');
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 30000}).toBeGreaterThan(30);
  const read = () => page.evaluate(() => {
    const copy = document.createElement('canvas');
    copy.width = 320; copy.height = 240;
    const ctx = copy.getContext('2d', {willReadFrequently: true});
    ctx.drawImage(document.querySelector('#screen'), 0, 0);
    return Array.from(ctx.getImageData(0, 0, 320, 1).data).join();
  });
  const first = await read();
  await expect.poll(read).not.toBe(first);
  expect(await page.evaluate(() => Module.finished)).toBeUndefined();
});
