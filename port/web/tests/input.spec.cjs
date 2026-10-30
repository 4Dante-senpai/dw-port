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
// PadRead swaps the two wire bytes (port/psyq/libetc.c).
const padRead = wire => ((wire << 8) | (wire >> 8)) & 0xffff;
const input = page => page.evaluate(() => Module.input());
async function running(page, url = '/?frames=1000000') {
  await page.goto(url);
  await expect.poll(() => page.evaluate(() => window.Module?.input?.().latchedSeq ?? 0), {timeout: 30000}).toBeGreaterThan(0);
  // Past the boot frames, so rate windows measure steady pacing (as in shell.spec.cjs).
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 30000}).toBeGreaterThan(120);
}
// The first latch that consumed sample `seq` or a later one must already show `value`.
async function expectLatched(page, seq, value) {
  await expect.poll(async () => { const s = await input(page); return s.latchedSeq >= seq ? s.padValue : -1; })
    .toBe(value);
}
async function fakePad(page) {
  await page.evaluate(() => {
    window.fakePad = {id: 'fake standard pad', index: 0, connected: true, mapping: 'standard', axes: [0, 0, 0, 0],
      buttons: Array.from({length: 17}, () => ({pressed: false, touched: false, value: 0}))};
    Object.defineProperty(navigator, 'getGamepads', {configurable: true, value: () => [window.fakePad]});
    dispatchEvent(Object.assign(new Event('gamepadconnected'), {gamepad: window.fakePad}));
  });
}
const setPad = (page, change) => page.evaluate(change => {
  for (const [i, value] of Object.entries(change.buttons || {}))
    window.fakePad.buttons[i] = {pressed: value > 0.5, touched: value > 0, value};
  if (change.axes) window.fakePad.axes = change.axes;
}, change);
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
  // Triggers count past half travel only.
  await setPad(page, {buttons: {6: 0.4}});
  await page.waitForTimeout(200);
  await expectLatched(page, (await input(page)).seq + 1, 0);
  // Keyboard and gamepad combine.
  await setPad(page, {buttons: {0: 1}});
  await page.keyboard.down('Enter');
  await expectLatched(page, (await input(page)).seq + 1, padRead((1 << 14) | (1 << 3)));
  await page.keyboard.up('Enter');
  await setPad(page, {buttons: {0: 0}});
  expect(failures).toEqual([]);
  evidence(info, 'gamepad', result);
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
  await expect.poll(() => page.evaluate(() => Module.finished), {timeout: 60000}).toBe(1);
  expect(await input(page)).toMatchObject({latchedSeq: 0, latchedVblank: 0, padValue: 0});
  await page.keyboard.up('Enter');
  expect(failures).toEqual([]);
});
