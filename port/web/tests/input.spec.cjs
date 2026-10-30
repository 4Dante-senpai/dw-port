const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const path = require('path');
const {errors} = require('./storage.cjs');
const RATE = 60.0087172527;
// docs/port/controls.md: wire bit of each PlayStation button and its default keys / standard buttons.
const BUTTONS = [
  {name: 'Select', bit: 0, keys: ['Backspace'], pad: 8},
  {name: 'Start', bit: 3, keys: ['Enter'], pad: 9},
  {name: 'Up', bit: 4, keys: ['ArrowUp', 'w'], pad: 12},
  {name: 'Right', bit: 5, keys: ['ArrowRight', 'd'], pad: 15},
  {name: 'Down', bit: 6, keys: ['ArrowDown', 's'], pad: 13},
  {name: 'Left', bit: 7, keys: ['ArrowLeft', 'a'], pad: 14},
  {name: 'L2', bit: 8, keys: ['1'], pad: 6},
  {name: 'R2', bit: 9, keys: ['3'], pad: 7},
  {name: 'L1', bit: 10, keys: ['q'], pad: 4},
  {name: 'R1', bit: 11, keys: ['e'], pad: 5},
  {name: 'Triangle', bit: 12, keys: ['i'], pad: 3},
  {name: 'Circle', bit: 13, keys: ['l'], pad: 1},
  {name: 'Cross', bit: 14, keys: ['k'], pad: 0},
  {name: 'Square', bit: 15, keys: ['j'], pad: 2}
];
const {padRead, input, running, expectLatched, fakePad, setPad} = require('./input_helpers.cjs');
function evidence(info, name, data) {
  fs.writeFileSync(path.join(process.env.DW1_WEB_BUILD, `input-${name}-${info.project.name}.json`), JSON.stringify(data, null, 2));
}

test('each of the 14 buttons reaches PadRead from the keyboard', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page), result = [];
  await running(page);
  for (const button of BUTTONS) for (const key of button.keys) {
    await page.keyboard.down(key);
    const pressed = (await input(page)).seq;
    await expectLatched(page, pressed, padRead(1 << button.bit));
    const latch = await input(page);
    await page.keyboard.up(key);
    await expectLatched(page, (await input(page)).seq, 0);
    result.push({button: button.name, key, padRead: latch.padValue, latchedVblank: latch.latchedVblank});
  }
  expect(failures).toEqual([]);
  evidence(info, 'keyboard', result);
});
test('each of the 14 buttons reaches PadRead from a standard gamepad', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page), result = [];
  await running(page);
  await fakePad(page);
  for (const button of BUTTONS) {
    await setPad(page, {buttons: {[button.pad]: 1}});
    await expectLatched(page, (await input(page)).seq + 1, padRead(1 << button.bit));
    await setPad(page, {buttons: {[button.pad]: 0}});
    await expectLatched(page, (await input(page)).seq + 1, 0);
    result.push({button: button.name, standard: button.pad});
  }
  // Triggers count past half travel, whatever the browser reports as `pressed`.
  await setPad(page, {buttons: {6: {pressed: true, value: 0.4}}});
  await expectLatched(page, (await input(page)).seq + 1, 0);
  await setPad(page, {buttons: {6: 0, 7: {pressed: false, value: 0.6}}});
  await expectLatched(page, (await input(page)).seq + 1, padRead(1 << 9));
  await setPad(page, {buttons: {7: 0}});
  // Keyboard and gamepad combine.
  await setPad(page, {buttons: {0: 1}});
  await page.keyboard.down('Enter');
  await expectLatched(page, (await input(page)).seq + 1, padRead((1 << 14) | (1 << 3)));
  await page.keyboard.up('Enter');
  await setPad(page, {buttons: {0: 0}});
  expect(failures).toEqual([]);
  evidence(info, 'gamepad', result);
});
test('a sample reaches PadRead at the first VBlank after it was written', async ({page}, info) => {
  const failures = errors(page);
  await running(page);
  // In the page, so no round trip to the test separates the write from the latch. Yielding, not
  // spinning: in Firefox the game thread's glReadPixels waits for the main thread (spec 4.5).
  const latches = await page.evaluate(async () => {
    const channel = new MessageChannel(), result = [];
    const next = () => new Promise(resolve => { channel.port1.onmessage = resolve; channel.port2.postMessage(0); });
    for (let i = 0; i < 60; i++) for (const type of ['keydown', 'keyup']) {
      const before = Module.input().latchedVblank;
      dispatchEvent(new KeyboardEvent(type, {code: 'KeyK'}));
      const {seq} = Module.input();
      let latch;
      do { latch = Module.input(); await next(); } while (latch.latchedSeq < seq);
      result.push({vblanks: latch.latchedVblank - before, padValue: latch.padValue});
    }
    return result;
  });
  latches.forEach((latch, i) => expect(latch.padValue).toBe(i % 2 ? 0 : padRead(1 << 14)));
  // One VBlank, or two if the write raced the latch; a poll PsyQ skipped delays one more.
  const late = latches.filter(latch => latch.vblanks > 2);
  expect(late.length, JSON.stringify(late)).toBeLessThanOrEqual(1);
  expect(failures).toEqual([]);
  evidence(info, 'latency', latches.map(latch => latch.vblanks));
});
test('all standard gamepads combine; other layouts are ignored', async ({page}) => {
  const failures = errors(page);
  await running(page);
  await fakePad(page, [null, 'standard', '', 'standard']);
  await setPad(page, {buttons: {0: 1}}, 1);
  await setPad(page, {buttons: {9: 1}}, 3);
  // A pad without the standard mapping has unknown indices.
  await setPad(page, {buttons: {1: 1, 2: 1}}, 2);
  await expectLatched(page, (await input(page)).seq + 1, padRead((1 << 14) | (1 << 3)));
  expect(failures).toEqual([]);
});
test('modified keys and focused Shell controls do not press buttons', async ({page}) => {
  const failures = errors(page);
  await running(page);
  await page.keyboard.press('Control+k');
  await page.keyboard.down('Alt'); await page.keyboard.down('j');
  await expectLatched(page, (await input(page)).seq + 1, 0);
  await page.keyboard.up('j'); await page.keyboard.up('Alt');
  await page.locator('#pause').focus();
  await page.keyboard.down('Enter');
  // Enter activated Pausa (so no VBlank latches now) and was not published as Start.
  await page.waitForTimeout(100);
  await page.evaluate(() => dispatchEvent(new KeyboardEvent('keydown', {code: 'Enter', repeat: true})));
  expect((await input(page)).mask).toBe(0);
  await page.keyboard.up('Enter');
  await expect(page.locator('#pause')).toHaveText('Reanudar');
  expect(failures).toEqual([]);
});
test('a clicked Pausa button leaves the keyboard to the game', async ({page}) => {
  const failures = errors(page);
  await running(page);
  await page.locator('#pause').click();
  await page.locator('#pause').click();
  await page.keyboard.down('Enter');
  await expectLatched(page, (await input(page)).seq, padRead(1 << 3));
  await page.keyboard.up('Enter');
  expect(failures).toEqual([]);
});
test('left stick acts as the D-pad only with the option on', async ({page}) => {
  const failures = errors(page);
  await running(page);
  await fakePad(page);
  const directions = [[[0, -1], 4], [[1, 0], 5], [[0, 1], 6], [[-1, 0], 7]];
  for (const [axes] of directions) {
    await setPad(page, {axes: [...axes, 0, 0]});
    await page.waitForTimeout(100);
    await expectLatched(page, (await input(page)).seq + 1, 0);
  }
  await page.evaluate(() => { document.querySelector('#stick-dpad').checked = true; });
  for (const [axes, bit] of directions) {
    await setPad(page, {axes: [...axes, 0, 0]});
    await expectLatched(page, (await input(page)).seq + 1, padRead(1 << bit));
  }
  // Inside the 0.5 dead zone nothing is pressed.
  await setPad(page, {axes: [0.4, -0.4, 0, 0]});
  await expectLatched(page, (await input(page)).seq + 1, 0);
  expect(failures).toEqual([]);
});
test('connecting and disconnecting a gamepad keeps the game paced', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await running(page);
  const sample = () => page.evaluate(() => ({t: performance.now(), ...Module.control()}));
  const a = await sample();
  await fakePad(page);
  await setPad(page, {buttons: {0: 1}});
  await expectLatched(page, (await input(page)).seq + 1, padRead(1 << 14));
  await page.evaluate(() => {
    window.fakePad.connected = false;
    Object.defineProperty(navigator, 'getGamepads', {configurable: true, value: () => [null]});
    dispatchEvent(Object.assign(new Event('gamepaddisconnected'), {gamepad: window.fakePad}));
  });
  await expectLatched(page, (await input(page)).seq + 1, 0);
  await page.waitForTimeout(2000);
  const b = await sample();
  const rate = (b.vblanks - a.vblanks) * 1000 / (b.t - a.t);
  expect(Math.abs(rate - RATE) / RATE, `${rate} VBlank/s`).toBeLessThan(0.05);
  expect(b.resynchronizations).toBe(0);
  expect(b.pauses).toBe(0);
  expect(b.held).toBe(0);
  expect(failures).toEqual([]);
  evidence(info, 'hotplug', {rate, start: a, end: b});
});
test('losing focus releases the keyboard', async ({page}) => {
  const failures = errors(page);
  await running(page);
  await page.keyboard.down('k');
  await expectLatched(page, (await input(page)).seq, padRead(1 << 14));
  await page.evaluate(() => dispatchEvent(new Event('blur')));
  await expectLatched(page, (await input(page)).seq, 0);
  await page.keyboard.up('k');
  expect(failures).toEqual([]);
});
test('scene captures apply no live input', async ({page}) => {
  const failures = errors(page);
  // A mark the game never reaches keeps the capture active; the run ends at its frame limit.
  await page.goto('/?frames=600&capture-mark=neverReached&capture-vblank=1');
  await expect.poll(() => page.evaluate(() => typeof window.Module?.input)).toBe('function');
  await page.keyboard.down('Enter');
  // The sample was published, so only the capture guard keeps it from the game.
  expect(await input(page)).toMatchObject({mask: 1 << 3});
  await expect.poll(() => page.evaluate(() => Module.finished), {timeout: 60000}).toBe(1);
  await expect(page.locator('#status')).toContainText('reached frame 599');
  expect(await input(page)).toMatchObject({latchedSeq: 0, latchedVblank: 0, padValue: 0});
  await page.keyboard.up('Enter');
  expect(failures).toEqual([]);
});
