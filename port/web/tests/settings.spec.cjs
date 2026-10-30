const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const path = require('path');
const {errors} = require('./storage.cjs');
const {padRead, input, running, expectLatched, fakePad, setPad} = require('./input_helpers.cjs');
const CROSS = 1 << 14, CIRCLE = 1 << 13, SQUARE = 1 << 15;
// docs/port/controls.md, as PortSettings v2.
const DEFAULTS = {schema_version: 2, stick_dpad: false, controls: {
  keyboard: {select: ['Backspace'], start: ['Enter'], up: ['ArrowUp', 'KeyW'], right: ['ArrowRight', 'KeyD'],
    down: ['ArrowDown', 'KeyS'], left: ['ArrowLeft', 'KeyA'], l2: ['Digit1'], r2: ['Digit3'], l1: ['KeyQ'],
    r1: ['KeyE'], triangle: ['KeyI'], circle: ['KeyL'], cross: ['KeyK'], square: ['KeyJ']},
  gamepad: {select: [8], start: [9], up: [12], right: [15], down: [13], left: [14], l2: [6], r2: [7], l1: [4],
    r1: [5], triangle: [3], circle: [1], cross: [0], square: [2]}}};
// IndexedDB "dw1" / "settings" / "port_settings", read and written from a page without the Shell.
function store(page, value) {
  return page.evaluate(value => new Promise((resolve, reject) => {
    const open = indexedDB.open('dw1', 1);
    open.onupgradeneeded = () => open.result.createObjectStore('settings');
    open.onerror = () => reject(open.error);
    open.onsuccess = () => {
      const db = open.result, tx = db.transaction('settings', value === undefined ? 'readonly' : 'readwrite');
      const request = value === undefined ? tx.objectStore('settings').get('port_settings')
        : tx.objectStore('settings').put(value, 'port_settings');
      tx.oncomplete = () => { db.close(); resolve(value === undefined ? request.result : undefined); };
      tx.onerror = tx.onabort = () => reject(tx.error || Error('aborted'));
    };
  }), value);
}
async function seed(page, value) { await page.goto('/seed.html'); await store(page, value); }
async function record(page) { await page.goto('/seed.html'); return store(page); }
async function options(page) {
  await page.goto('/');
  await expect(page.locator('#launcher')).toBeVisible();
  await page.locator('#options summary').click();
  await expect(page.locator('#controls tbody tr')).toHaveCount(14);
}
const cell = (page, button, kind) => page.locator(`#controls tr[data-button=${button}] .${kind}`);
async function expectKey(page, key, value) {
  await page.keyboard.down(key);
  await expectLatched(page, (await input(page)).seq, padRead(value));
  await page.keyboard.up(key);
  await expectLatched(page, (await input(page)).seq, 0);
}
function evidence(info, name, data) {
  fs.writeFileSync(path.join(process.env.DW1_WEB_BUILD, `settings-${name}-${info.project.name}.json`), JSON.stringify(data, null, 2));
}

test('a reassigned key survives a reload and reaches PadRead', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await page.locator('#controls tr[data-button=cross] .remap').click();
  await expect(page.locator('#remap-status')).toContainText('Cross');
  await page.keyboard.press('z');
  await expect(cell(page, 'cross', 'keys')).toHaveText('KeyZ');
  await expect.poll(async () => (await store(page))?.controls.keyboard.cross).toEqual(['KeyZ']);
  await page.reload();
  await page.locator('#options summary').click();
  await expect(cell(page, 'cross', 'keys')).toHaveText('KeyZ');
  const stored = await record(page);
  expect(stored.schema_version).toBe(2);
  expect(stored.controls.keyboard.cross).toEqual(['KeyZ']);
  await running(page);
  await expectKey(page, 'z', CROSS);
  await expectKey(page, 'k', 0);
  expect(failures).toEqual([]);
  evidence(info, 'keyboard', stored);
});
test('a reassigned gamepad button survives a reload and reaches PadRead', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await fakePad(page);
  await page.locator('#controls tr[data-button=circle] .remap').click();
  await setPad(page, {buttons: {16: 1}});
  await expect(cell(page, 'circle', 'pads')).toHaveText('16');
  await setPad(page, {buttons: {16: 0}});
  await expect.poll(async () => (await store(page))?.controls.gamepad.circle).toEqual([16]);
  await page.reload();
  await running(page);
  await fakePad(page);
  await setPad(page, {buttons: {16: 1}});
  await expectLatched(page, (await input(page)).seq + 1, padRead(CIRCLE));
  await setPad(page, {buttons: {16: 0, 1: 1}});
  await expectLatched(page, (await input(page)).seq + 1, 0);
  expect(failures).toEqual([]);
});
test('a gamepad button held when the capture starts does not count', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await fakePad(page);
  await setPad(page, {buttons: {0: 1}});
  await page.locator('#controls tr[data-button=circle] .remap').click();
  await page.waitForTimeout(300);
  await expect(cell(page, 'circle', 'pads')).toHaveText('1');
  await setPad(page, {buttons: {16: 1}});
  await expect(cell(page, 'circle', 'pads')).toHaveText('16');
  await expect(cell(page, 'cross', 'pads')).toHaveText('0');
  expect(failures).toEqual([]);
});
test('modifiers, Tab and autorepeat are not captured; a pending capture ends with the game', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await page.locator('#controls tr[data-button=start] .remap').click();
  for (const key of ['Control', 'Alt', 'Tab']) await page.keyboard.press(key);
  await page.evaluate(() => dispatchEvent(new KeyboardEvent('keydown', {code: 'KeyZ', repeat: true})));
  await expect(page.locator('#remap-status')).toContainText('Start');
  await expect(cell(page, 'start', 'keys')).toHaveText('Enter');
  // Stealing K from Cross reports it unbound.
  await page.keyboard.press('k');
  await expect(cell(page, 'start', 'keys')).toHaveText('KeyK');
  await expect(page.locator('#remap-status')).toContainText('Sin asignar en teclado: Cross');
  // A capture still open when the game boots does not take the game's first key.
  await page.locator('#controls tr[data-button=square] .remap').click();
  await page.evaluate(() => boot(['--frames', '1000000']));
  await expect.poll(() => page.evaluate(() => window.Module?.control?.().vblanks ?? 0), {timeout: 30000}).toBeGreaterThan(120);
  await page.keyboard.down('k');
  await expectLatched(page, (await input(page)).seq, padRead(1 << 3));
  await page.keyboard.up('k');
  await expect(cell(page, 'square', 'keys')).toHaveText('KeyJ');
  expect(failures).toEqual([]);
});
test('without IndexedDB the game boots with the defaults and a warning', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await page.addInitScript(() => {
    IDBFactory.prototype.open = function () { throw new DOMException('blocked for the test', 'SecurityError'); };
  });
  await options(page);
  await expect(page.locator('#settings-warning')).toContainText('No se pudieron leer las opciones');
  await running(page);
  await expectKey(page, 'k', CROSS);
  expect(failures).toEqual([]);
});
test('a key taken by another button leaves its old button; Escape cancels', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await page.locator('#controls tr[data-button=triangle] .remap').click();
  await page.keyboard.press('Escape');
  await expect(page.locator('#remap-status')).toHaveText('');
  await expect(cell(page, 'triangle', 'keys')).toHaveText('KeyI');
  await page.locator('#controls tr[data-button=square] .remap').click();
  await page.keyboard.press('k');
  await expect(cell(page, 'square', 'keys')).toHaveText('KeyK');
  await expect(cell(page, 'cross', 'keys')).toHaveText('—');
  await running(page);
  await expectKey(page, 'k', SQUARE);
  await expectKey(page, 'j', 0);
  expect(failures).toEqual([]);
});
test('Restaurar valores por defecto returns to docs/port/controls.md', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await page.locator('#controls tr[data-button=cross] .remap').click();
  await page.keyboard.press('z');
  await page.locator('#stick-dpad').check();
  await expect(cell(page, 'cross', 'keys')).toHaveText('KeyZ');
  await page.locator('#restore-defaults').click();
  await expect(cell(page, 'cross', 'keys')).toHaveText('KeyK');
  await expect(page.locator('#stick-dpad')).not.toBeChecked();
  await expect.poll(() => store(page)).toEqual(DEFAULTS);
  expect(await record(page)).toEqual(DEFAULTS);
  await running(page);
  await expectKey(page, 'k', CROSS);
  await expectKey(page, 'z', 0);
  expect(failures).toEqual([]);
});
test('the stick option persists and drives the D-pad', async ({page}) => {
  test.setTimeout(120000);
  const failures = errors(page);
  await options(page);
  await page.locator('#stick-dpad').check();
  await expect.poll(async () => (await store(page))?.stick_dpad).toBe(true);
  await page.reload();
  await expect(page.locator('#stick-dpad')).toBeChecked();
  await running(page);
  await fakePad(page);
  await setPad(page, {axes: [0, -1, 0, 0]});
  await expectLatched(page, (await input(page)).seq + 1, padRead(1 << 4));
  expect(failures).toEqual([]);
});
test('a schema_version 1 record migrates without losing data', async ({page}, info) => {
  test.setTimeout(120000);
  const failures = errors(page);
  const v1 = {schema_version: 1, stick_dpad: true, keyboard: {KeyZ: 'cross', KeyX: 'circle', Enter: 'start'},
    unknown_future_field: {kept: [1, 2, 3]}};
  await seed(page, v1);
  await options(page);
  await expect(cell(page, 'cross', 'keys')).toHaveText('KeyZ');
  await expect(page.locator('#stick-dpad')).toBeChecked();
  const migrated = await record(page);
  expect(migrated).toEqual({schema_version: 2, stick_dpad: true, unknown_future_field: {kept: [1, 2, 3]},
    controls: {keyboard: {...Object.fromEntries(Object.keys(DEFAULTS.controls.keyboard).map(b => [b, []])),
      cross: ['KeyZ'], circle: ['KeyX'], start: ['Enter']}, gamepad: DEFAULTS.controls.gamepad}});
  await running(page);
  await expectKey(page, 'z', CROSS);
  await expectKey(page, 'x', CIRCLE);
  await expectKey(page, 'k', 0);
  expect(failures).toEqual([]);
  evidence(info, 'migration', {seeded: v1, migrated});
});
for (const [name, value, text] of [['newer', {schema_version: 99, anything: true}, 'más nueva'],
  ['unreadable', 'not an object', 'no se pueden leer'],
  ['duplicated', {...DEFAULTS, controls: {...DEFAULTS.controls,
    keyboard: {...DEFAULTS.controls.keyboard, square: ['KeyK']}}}, 'no se pueden leer'],
  ['mistyped', {...DEFAULTS, controls: {...DEFAULTS.controls,
    gamepad: {...DEFAULTS.controls.gamepad, cross: ['x']}}}, 'no se pueden leer'],
  ['malformed', {schema_version: 2, stick_dpad: 'yes', controls: {}}, 'no se pueden leer']]) {
  test(`a ${name} record runs the defaults and is never overwritten`, async ({page}) => {
    test.setTimeout(120000);
    const failures = errors(page);
    await seed(page, value);
    await options(page);
    await expect(page.locator('#settings-warning')).toContainText(text);
    await page.locator('#controls tr[data-button=cross] .remap').click();
    await page.keyboard.press('z');
    await expect(cell(page, 'cross', 'keys')).toHaveText('KeyZ');
    expect(await record(page)).toEqual(value);
    await running(page);
    await expectKey(page, 'k', CROSS);
    expect(failures).toEqual([]);
  });
}
