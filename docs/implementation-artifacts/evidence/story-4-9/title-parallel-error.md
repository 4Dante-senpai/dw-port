# Instructions

- Following Playwright test failed.
- Explain why, be concise, respect Playwright best practices.
- Provide a snippet of code with the fix, if possible.

# Test info

- Name: title.spec.cjs >> one click on Jugar reaches the title and Start leaves it
- Location: ../../../port/web/tests/title.spec.cjs:78:1

# Error details

```
Error: expect(received).toBe(expected) // Object.is equality

Expected: true
Received: false

Call Log:
- Timeout 10000ms exceeded while waiting on the predicate
```

# Page snapshot

```yaml
- generic [active] [ref=f1e1]:
  - generic [ref=f1e2]:
    - button "Pausa" [ref=f1e4]
    - generic [ref=f1e5]:
      - text: Volumen
      - slider "Volumen" [ref=f1e6]: "100"
      - status [ref=f1e7]: 100 %
  - status
  - status [ref=f1e8]: Modo sin conexión no disponible.
  - status
  - generic [ref=f1e9]: "port: stack verified 0xa6f638 dw1: pacing resynchronization vblank=4027 late_ns=74332925 dw1: pacing resynchronization vblank=7337 late_ns=81282175 dw1: pacing resynchronization vblank=7591 late_ns=73436636 dw1: pacing resynchronization vblank=7598 late_ns=69700362 dw1: pacing resynchronization vblank=7738 late_ns=72535734 dw1: pacing resynchronization vblank=7776 late_ns=66728461 dw1: pacing resynchronization vblank=7841 late_ns=70449127 dw1: pacing resynchronization vblank=7876 late_ns=72481326"
```

# Test source

```ts
  26  |     // The visible canvas shows the same frame the capture took.
  27  |     await expect.poll(() => page.evaluate(() => {
  28  |       const bytes = Uint8Array.from(Module.capture);
  29  |       const fields = new TextDecoder().decode(bytes.slice(0, 32)).match(/^P6\s+(\d+)\s+(\d+)\s+255\s/);
  30  |       const [width, height] = [Number(fields[1]), Number(fields[2])];
  31  |       const pixels = bytes.subarray(fields[0].length);
  32  |       const copy = document.createElement('canvas');
  33  |       copy.width = width; copy.height = height;
  34  |       const ctx = copy.getContext('2d');
  35  |       ctx.drawImage(document.querySelector('#screen'), 0, 0);
  36  |       const shown = ctx.getImageData(0, 0, width, height).data;
  37  |       let different = 0;
  38  |       for (let i = 0; i < width * height; i++)
  39  |         for (let c = 0; c < 3; c++) if (shown[4 * i + c] !== pixels[3 * i + c]) { different++; break; }
  40  |       return different;
  41  |     })).toBe(0);
  42  |     const storageAccesses = await page.evaluate(() => Module.storageAccesses);
  43  |     expect(storageAccesses).toBe(0);
  44  |     expect(failures).toEqual([]);
  45  |     fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-${info.project.name}-${poison}.json`,
  46  |       JSON.stringify({sha256:hash, native_sha256:process.env.DW1_NATIVE_TITLE_SHA, poison, storageAccesses}));
  47  |   });
  48  | }
  49  | const seedPack = async page => {
  50  |   await page.goto('/');
  51  |   await expect(page.locator('#load-pack')).toBeEnabled();
  52  |   await page.locator('#load-pack').click();
  53  |   await expect(page.locator('#pack-status')).toHaveText('Pack disponible', {timeout:120000});
  54  | };
  55  | test('a reassigned Start leaves the title after a reload', async ({page}) => {
  56  |   test.setTimeout(420000);
  57  |   const failures = errors(page);
  58  |   await seedPack(page);
  59  |   await page.locator('#options summary').click();
  60  |   await page.locator('#controls tr[data-button=start] .remap').click();
  61  |   await page.keyboard.press('z');
  62  |   await expect.poll(async () => (await store(page))?.controls.keyboard.start).toEqual(['KeyZ']);
  63  |   await page.reload();
  64  |   await expect(page.locator('#play')).toBeEnabled();
  65  |   await page.locator('#play').click();
  66  |   const marks = () => page.evaluate(() => Module.marks || []);
  67  |   await expect.poll(marks, {timeout:360000}).toContain('runLandingScreen');
  68  |   await page.waitForTimeout(1000);
  69  |   // Enter no longer means Start.
  70  |   await page.keyboard.press('Enter', {delay:200});
  71  |   await page.waitForTimeout(1000);
  72  |   expect(await marks()).not.toContain('runMainMenu');
  73  |   await page.keyboard.down('z');
  74  |   await expect.poll(marks, {timeout:10000}).toContain('runMainMenu');
  75  |   await page.keyboard.up('z');
  76  |   expect(failures).toEqual([]);
  77  | });
  78  | test('one click on Jugar reaches the title and Start leaves it', async ({page}, info) => {
  79  |   test.setTimeout(420000);
  80  |   const failures = errors(page);
  81  |   await seedPack(page);
  82  |   // A fresh document: the fixture click above must not count as the gesture.
  83  |   await page.addInitScript(() => {
  84  |     const Native = AudioContext;
  85  |     window.audioLog = [];
  86  |     window.AudioContext = class extends Native {
  87  |       constructor(...args) { super(...args); audioLog.push(navigator.userActivation.isActive); }
  88  |     };
  89  |   });
  90  |   await page.goto('/');
  91  |   await page.waitForTimeout(1500);
  92  |   const start = Date.now();
  93  |   await page.locator('#play').click();
  94  |   expect(await page.evaluate(() => window.audioLog)).toEqual([true]);
  95  |   await expect.poll(() => page.evaluate(() => Module.audioContext?.state)).toBe('running');
  96  |   // Paced at 60 Hz: the title takes as long as on the console.
  97  |   await expect.poll(() => page.evaluate(() => Module.marks || []), {timeout:360000}).toContain('runLandingScreen');
  98  |   await expect.poll(() => page.evaluate(() => {
  99  |     const copy = document.createElement('canvas');
  100 |     copy.width = 320; copy.height = 240;
  101 |     const ctx = copy.getContext('2d');
  102 |     ctx.drawImage(document.querySelector('#screen'), 0, 0);
  103 |     return ctx.getImageData(0, 0, 320, 240).data.some((v, i) => i % 4 !== 3 && v);
  104 |   })).toBe(true);
  105 |   const marks = () => page.evaluate(() => Module.marks || []);
  106 |   // The title waits 600 VBlanks for Start; without it the attract loop never opens the menu.
  107 |   expect(await marks()).not.toContain('runMainMenu');
  108 |   await page.evaluate(() => {
  109 |     const analyser = Module.audioContext.createAnalyser();
  110 |     Module.audioGain.connect(analyser); Module.outputAnalyser = analyser;
  111 |   });
  112 |   // runLandingScreen is silent until its original Start sound. Observe that
  113 |   // sound during the title's 40-VBlank fade, before runMainMenu is entered.
  114 |   await page.keyboard.down('Enter');
  115 |   let signal;
  116 |   await expect.poll(async () => {
  117 |     signal = await page.evaluate(() => {
  118 |       const samples = new Float32Array(Module.outputAnalyser.fftSize);
  119 |       Module.outputAnalyser.getFloatTimeDomainData(samples);
  120 |       return {...Module.audioStats(), output_nonzero: samples.some(v => v !== 0),
  121 |         context_state: Module.audioContext.state,
  122 |         in_title: !Module.marks.includes('runMainMenu')};
  123 |     });
  124 |     return signal.in_title && signal.output_nonzero && signal.context_state === 'running' &&
  125 |       signal.level >= 6144 - 1536 && signal.level <= 6144 + 1536;
> 126 |   }, {timeout:10000, intervals:[16]}).toBe(true);
      |                                       ^ Error: expect(received).toBe(expected) // Object.is equality
  127 |   expect(signal.underruns).toBe(0); expect(signal.overflows).toBe(0);
  128 |   await expect.poll(marks, {timeout:10000}).toContain('runMainMenu');
  129 |   await page.keyboard.up('Enter');
  130 |   const control = await page.evaluate(() => Module.control());
  131 |   expect(control.resynchronizations).toBe(0);
  132 |   expect(failures).toEqual([]);
  133 |   fs.writeFileSync(`${process.env.DW1_WEB_BUILD}/title-play-${info.project.name}.json`,
  134 |     JSON.stringify({ms_to_title:Date.now() - start, control, audio: signal}));
  135 | });
  136 | 
```