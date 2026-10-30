// Shared by input.spec.cjs and settings.spec.cjs: drive the Shell's input and read the latch.
const {expect} = require('/opt/node_modules/@playwright/test');
// PadRead swaps the two wire bytes (port/psyq/libetc.c).
const padRead = wire => ((wire << 8) | (wire >> 8)) & 0xffff;
const input = page => page.evaluate(() => Module.input());
async function running(page, url = '/?frames=1000000') {
  await page.goto(url);
  await expect.poll(() => page.evaluate(() => window.Module?.input?.().latchedSeq ?? 0), {timeout: 30000}).toBeGreaterThan(0);
  // Past the boot frames, so rate windows measure steady pacing (as in shell.spec.cjs).
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 30000}).toBeGreaterThan(120);
}
// Wait for the first latch that consumed sample `seq` (or a later one), then check it once: the
// value must already be there, not after more retries.
async function expectLatched(page, seq, value) {
  await expect.poll(async () => (await input(page)).latchedSeq >= seq, {intervals: [5]}).toBe(true);
  const latch = await input(page);
  expect(latch.padValue, `sample ${seq} latched as ${latch.latchedSeq} at VBlank ${latch.latchedVblank}`).toBe(value);
}
// Pads by slot; `null` is an empty slot. Each one starts released.
async function fakePad(page, slots = ['standard']) {
  await page.evaluate(slots => {
    window.fakePads = slots.map((mapping, index) => mapping === null ? null : {id: 'fake pad ' + index, index,
      connected: true, mapping, axes: [0, 0, 0, 0],
      buttons: Array.from({length: 17}, () => ({pressed: false, touched: false, value: 0}))});
    window.fakePad = window.fakePads.find(Boolean);
    Object.defineProperty(navigator, 'getGamepads', {configurable: true, value: () => window.fakePads});
    for (const pad of window.fakePads.filter(Boolean))
      dispatchEvent(Object.assign(new Event('gamepadconnected'), {gamepad: pad}));
  }, slots);
}
// Button values (pressed past 0.5 unless given as {pressed, value}) on the pad in `slot`.
const setPad = (page, change, slot) => page.evaluate(([change, slot]) => {
  const pad = slot === undefined ? window.fakePad : window.fakePads[slot];
  for (const [i, value] of Object.entries(change.buttons || {}))
    pad.buttons[i] = typeof value === 'object' ? {touched: true, ...value} : {pressed: value > 0.5, touched: value > 0, value};
  if (change.axes) pad.axes = change.axes;
}, [change, slot]);
module.exports = {padRead, input, running, expectLatched, fakePad, setPad};
