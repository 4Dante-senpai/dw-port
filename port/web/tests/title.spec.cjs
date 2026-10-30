const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors} = require('./storage.cjs');
const crypto = require('crypto');
const fs = require('fs');
for (const poison of [false, true]) {
  test('title capture matches native' + (poison ? ' with storage poisoned' : ''), async ({page}, info) => {
    test.setTimeout(180000);
    const failures = errors(page);
    if (poison) {
      await seed(page, 'us.pack');
    } else {
      await page.goto('/');
      if (process.env.DW1_WEB_EXPECTED_COMMIT)
        await expect(page.locator('#version')).toHaveText(process.env.DW1_WEB_EXPECTED_COMMIT);
      await expect(page.locator('#load-pack')).toBeEnabled();
      await page.locator('#load-pack').click();
      await expect(page.locator('#pack-status')).toHaveText('Pack disponible', {timeout:120000});
      await expect(page.locator('#play')).toBeEnabled();
    }
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
test('one click on Jugar reaches the title with a running AudioContext', async ({page}, info) => {
  test.setTimeout(420000);
  const failures = errors(page);
  await page.goto('/');
  await expect(page.locator('#load-pack')).toBeEnabled();
  await page.locator('#load-pack').click();
  await expect(page.locator('#pack-status')).toHaveText('Pack disponible', {timeout:120000});
  // A fresh document: the fixture click above must not count as the gesture.
  await page.addInitScript(() => {
    window.DW1_PLAY_FRAMES = 1000000;
    const Native = AudioContext;
    window.audioLog = [];
    window.AudioContext = class extends Native {
      constructor(...args) { super(...args); audioLog.push(navigator.userActivation.isActive); }
    };
  });
  await page.goto('/');
  await page.waitForTimeout(1500);
  const start = Date.now();
  await page.locator('#play').click();
  expect(await page.evaluate(() => window.audioLog)).toEqual([true]);
  await expect.poll(() => page.evaluate(() => Module.audioContext?.state)).toBe('running');
  // Paced at 60 Hz: the title takes as long as on the console.
  await expect.poll(() => page.evaluate(() => Module.marks || []), {timeout:360000}).toContain('runLandingScreen');
  const control = await page.evaluate(() => Module.control());
  expect(control.resynchronizations).toBe(0);
  expect(failures).toEqual([]);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-play-${info.project.name}.json`,
    JSON.stringify({ms_to_title:Date.now() - start, control}));
});
