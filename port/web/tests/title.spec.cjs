const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const crypto = require('crypto');
const fs = require('fs');
for (const poison of [false, true]) {
  test('title capture matches native' + (poison ? ' with storage poisoned' : ''), async ({page}, info) => {
    test.setTimeout(180000);
    const failures = errors(page);
    await seed(page, 'us.pack');
    await page.goto('/?pack=1&frames=10000&capture-mark=runLandingScreen&capture-vblank=60' +
                    (poison ? '&poison=1' : ''));
    await expect.poll(() => page.evaluate(() => Module.finished), {timeout:120000}).toBe(0);
    const bytes = await page.evaluate(() => Module.capture);
    const hash = crypto.createHash('sha256').update(Buffer.from(bytes)).digest('hex');
    expect(hash).toBe(process.env.DW1_NATIVE_TITLE_SHA);
    const storageAccesses = await page.evaluate(() => Module.storageAccesses);
    expect(storageAccesses).toBe(0);
    expect(failures).toEqual([]);
    fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-${info.project.name}-${poison}.json`,
      JSON.stringify({sha256:hash, native_sha256:process.env.DW1_NATIVE_TITLE_SHA, poison, storageAccesses}));
  });
}
