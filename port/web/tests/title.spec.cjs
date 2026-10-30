const {test, expect} = require('/opt/node_modules/@playwright/test');
const {seed, errors, store} = require('./storage.cjs');
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
    await expect.poll(() => page.evaluate(() => window.Module?.finished), {timeout:120000}).toBe(0);
    const bytes = await page.evaluate(() => Module.capture);
    const hash = crypto.createHash('sha256').update(Buffer.from(bytes)).digest('hex');
    expect(hash).toBe(process.env.DW1_NATIVE_TITLE_SHA);
    // The visible canvas shows the same frame the capture took.
    await expect.poll(() => page.evaluate(() => {
      const bytes = Uint8Array.from(Module.capture);
      const fields = new TextDecoder().decode(bytes.slice(0, 32)).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
      const [width, height] = [Number(fields[1]), Number(fields[2])];
      const pixels = bytes.subarray(fields[0].length);
      const copy = document.createElement('canvas');
      copy.width = width; copy.height = height;
      const ctx = copy.getContext('2d');
      ctx.drawImage(document.querySelector('#screen'), 0, 0);
      const shown = ctx.getImageData(0, 0, width, height).data;
      let different = 0;
      for (let i = 0; i < width * height; i++)
        for (let c = 0; c < 3; c++) if (shown[4 * i + c] !== pixels[3 * i + c]) { different++; break; }
      return different;
    })).toBe(0);
    const storageAccesses = await page.evaluate(() => Module.storageAccesses);
    expect(storageAccesses).toBe(0);
    expect(failures).toEqual([]);
    fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-${info.project.name}-${poison}.json`,
      JSON.stringify({sha256:hash, native_sha256:process.env.DW1_NATIVE_TITLE_SHA, poison, storageAccesses}));
  });
}
const seedPack = async page => {
  await page.goto('/');
  await expect(page.locator('#load-pack')).toBeEnabled();
  await page.locator('#load-pack').click();
  await expect(page.locator('#pack-status')).toHaveText('Pack disponible', {timeout:120000});
};
test('a reassigned Start leaves the title after a reload', async ({page}) => {
  test.setTimeout(420000);
  const failures = errors(page);
  await seedPack(page);
  await page.locator('#options summary').click();
  await page.locator('#controls tr[data-button=start] .remap').click();
  await page.keyboard.press('z');
  await expect.poll(async () => (await store(page))?.controls.keyboard.start).toEqual(['KeyZ']);
  await page.reload();
  await expect(page.locator('#play')).toBeEnabled();
  await page.locator('#play').click();
  const marks = () => page.evaluate(() => Module.marks || []);
  await expect.poll(marks, {timeout:360000}).toContain('runLandingScreen');
  await page.waitForTimeout(1000);
  // Enter no longer means Start.
  await page.keyboard.press('Enter', {delay:200});
  await page.waitForTimeout(1000);
  expect(await marks()).not.toContain('runMainMenu');
  await page.keyboard.down('z');
  await expect.poll(marks, {timeout:10000}).toContain('runMainMenu');
  await page.keyboard.up('z');
  expect(failures).toEqual([]);
});
test('one click on Jugar reaches the title and Start leaves it', async ({page}, info) => {
  test.setTimeout(420000);
  const failures = errors(page);
  await seedPack(page);
  // A fresh document: the fixture click above must not count as the gesture.
  await page.addInitScript(() => {
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
  await expect.poll(() => page.evaluate(() => {
    const copy = document.createElement('canvas');
    copy.width = 320; copy.height = 240;
    const ctx = copy.getContext('2d');
    ctx.drawImage(document.querySelector('#screen'), 0, 0);
    return ctx.getImageData(0, 0, 320, 240).data.some((v, i) => i % 4 !== 3 && v);
  })).toBe(true);
  const marks = () => page.evaluate(() => Module.marks || []);
  // The title waits 600 VBlanks for Start; without it the attract loop never opens the menu.
  expect(await marks()).not.toContain('runMainMenu');
  await page.keyboard.down('Enter');
  await expect.poll(marks, {timeout:10000}).toContain('runMainMenu');
  await page.keyboard.up('Enter');
  const control = await page.evaluate(() => Module.control());
  expect(control.resynchronizations).toBe(0);
  expect(failures).toEqual([]);
  fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-play-${info.project.name}.json`,
    JSON.stringify({ms_to_title:Date.now() - start, control}));
});
