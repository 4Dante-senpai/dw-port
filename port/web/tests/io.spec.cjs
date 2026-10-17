const {test, expect} = require('/opt/node_modules/@playwright/test');
const fs = require('fs');
const {seed, errors} = require('./storage.cjs');
test('concurrent positional reads sleep while the worker delays responses', async ({page}, info) => {
  const failures = errors(page);
  await seed(page, 'synthetic.pack');
  await page.goto('/?io-test=1&poison=1');
  await expect.poll(() => page.evaluate(() => Module.finished)).toBe(0);
  const result = await page.evaluate(() => Module.ioResult);
  expect(result.failed).toBe(0);
  expect(result.requests).toBeGreaterThanOrEqual(80);
  expect(result.waits).toBeGreaterThanOrEqual(result.requests);
  expect(result.waits).toBeLessThanOrEqual(result.requests * 2);
  result.storageAccesses = await page.evaluate(() => Module.storageAccesses);
  expect(result.storageAccesses).toBe(0);
  expect(failures).toEqual([]);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/io-${info.project.name}.json`, JSON.stringify(result));
});
test('storage guard records exactly one hit per reader pthread', async ({page}, info) => {
  const failures = errors(page);
  await seed(page, 'synthetic.pack');
  await page.goto('/?io-test=guard&poison=1');
  await expect.poll(() => page.evaluate(() => Module.finished)).toBe(0);
  const result = await page.evaluate(() => ({
    ...Module.ioResult,
    storageAccesses: Module.storageAccesses,
    storageAccessThreads: Module.storageAccessThreads
  }));
  expect(result.failed).toBe(0);
  expect(result.storageAccesses).toBe(4);
  expect(result.storageAccessThreads).toHaveLength(4);
  expect(new Set(result.storageAccessThreads).size).toBe(4);
  expect(failures).toEqual([]);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/io-guard-${info.project.name}.json`, JSON.stringify(result));
});
test('missing Pack prevents game startup', async ({page}) => {
  const failures = errors(page);
  await page.goto('/?pack=1');
  await expect(page.locator('#status')).toContainText('cannot open dw1/us.pack');
  expect(await page.evaluate(() => Module.finished)).toBe(1);
  expect(await page.locator('#status').textContent()).not.toContain('stack verified');
  expect(failures).toEqual([]);
});
for (const [file, message] of [['corrupt.pack', 'not a Pack'], ['synthetic.pack', 'another Manifest'],
                                ['bad-index.pack', 'bad header or index']]) {
  test('rejects ' + file, async ({page}) => {
    const failures = errors(page);
    await seed(page, file);
    await page.goto('/?pack=1&poison=1');
    await expect(page.locator('#status')).toContainText(message);
    await expect.poll(() => page.evaluate(() => Module.finished)).toBe(1);
    expect(await page.locator('#status').textContent()).not.toContain('stack verified');
    expect(failures).toEqual([]);
  });
}
