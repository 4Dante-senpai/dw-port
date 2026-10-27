const fs = require('fs');
const path = require('path');
const {expect} = require('/opt/node_modules/@playwright/test');
function disc() {
  const raw = 2352, bytes = Buffer.alloc(32 * raw);
  const user = n => bytes.subarray(n * raw + 24, n * raw + 24 + 2048);
  const bcd = n => ((Math.floor(n / 10) << 4) | n % 10);
  for (let i = 0; i < 32; i++) {
    const s = bytes.subarray(i * raw, (i + 1) * raw), a = i + 150;
    s.fill(255, 1, 11); s[12] = bcd(Math.floor(a / 4500));
    s[13] = bcd(Math.floor(a / 75) % 60); s[14] = bcd(a % 75); s[15] = 2;
    s[18] = s[22] = i === 27 || i === 29 ? 0x24 : 0x08;
    for (let k = 24; k < raw; k++) s[k] = (i * 31 + k * 7) & 255;
  }
  for (let i = 16; i <= 21; i++) user(i).fill(0);
  function record(dst, pos, name, lba, size, dir) {
    const n = Buffer.from(name), len = (33 + n.length + 1) & ~1;
    const d = dst.subarray(pos, pos + len); d.fill(0); d[0] = len;
    d.writeUInt32LE(lba, 2); d.writeUInt32BE(lba, 6);
    d.writeUInt32LE(size, 10); d.writeUInt32BE(size, 14);
    d[25] = dir ? 2 : 0; d[28] = d[31] = 1; d[32] = n.length; n.copy(d, 33);
    return len;
  }
  user(16)[0] = 1; user(16).write('CD001', 1);
  record(user(16), 156, '\0', 20, 2048, true);
  let p = 0;
  for (const args of [['\0',20,2048,true], ['\x01',20,2048,true], ['MOVIE',21,2048,true],
    ['SLUS_010.32;1',22,5000,false], ['SYSTEM.CNF;1',25,100,false], ['MOV_REL.BIN;1',30,3000,false]])
    p += record(user(20), p, ...args);
  p = 0;
  for (const args of [['\0',21,2048,true], ['\x01',20,2048,true], ['OP1.STR;1',26,8192,false]])
    p += record(user(21), p, ...args);
  return bytes;
}
const bin = buffer => ({name:'disc.bin', mimeType:'application/octet-stream', buffer});
const cue = (mode = 'MODE2/2352', name = 'disc.bin') => ({name:'disc.cue', mimeType:'text/plain',
  buffer:Buffer.from(`FILE "${name}" BINARY\n TRACK 01 ${mode}\n INDEX 01 00:00:00\n`)});
async function manifest(page, bytes) {
  await page.goto('/seed.html');
  return page.evaluate(async data => new Promise((resolve, reject) => {
    const worker = new Worker('/io_worker.js');
    worker.onmessage = ({data}) => {
      if (data.progress !== undefined) return;
      worker.terminate(); data.error ? reject(Error(data.error)) : resolve(data.manifest);
    };
    worker.onerror = e => {worker.terminate(); reject(Error(e.message));};
    worker.postMessage({type:'manifest', files:[new File([new Uint8Array(data)], 'disc.bin')]});
  }), [...bytes]);
}
async function launch(page, tsv) {
  await page.route('**/__fixture/us.pack.sha256', r => r.fulfill({status:404, body:''}));
  if (tsv) await page.route('**/us.tsv', r => r.fulfill({body:tsv}));
  await page.goto('/');
  await expect(page.locator('#pack-status')).toHaveText('Pack ausente');
  await expect(page.locator('#disc')).toBeEnabled();
}
async function entries(page) {
  return page.evaluate(async () => {
    try {
      const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
      const names = []; for await (const name of dir.keys()) names.push(name);
      return names.sort();
    } catch (e) {if (e.name === 'NotFoundError') return []; throw e;}
  });
}
async function memoryStart(page, browserName, gate = false) {
  if (browserName !== 'chromium') return;
  if (gate) {
    // Keep the short synthetic validation alive while Chromium samples its worker heap.
    await page.route('**/io_worker.js', async route => {
      const response = await route.fetch();
      const source = (await response.text()).replace("    call(['build',", `    await new Promise(resolve => {
        const receive = onmessage;
        onmessage = () => { onmessage = receive; resolve(); };
        postMessage({memoryGate:true});
      });
      call(['build',`);
      await route.fulfill({response, body:source});
    });
    await page.evaluate(() => {
      const NativeWorker = Worker;
      window.Worker = class extends NativeWorker {
        constructor(...args) {
          super(...args);
          this.addEventListener('message', event => {
            if (!event.data.memoryGate) return;
            event.stopImmediatePropagation();
            performance.measureUserAgentSpecificMemory().then(sample => {
              window.memoryMax = Math.max(window.memoryMax, sample.bytes);
              this.postMessage({resume:true});
            });
          });
        }
      };
    });
  }
  await page.evaluate(() => {
    window.memoryStop = false; window.memoryMax = 0;
    window.memoryDone = (async () => {
      do {
        const sample = await performance.measureUserAgentSpecificMemory();
        window.memoryMax = Math.max(window.memoryMax, sample.bytes);
        await new Promise(resolve => setTimeout(resolve, 200));
      } while (!window.memoryStop);
    })();
  });
}
async function memoryEnd(page, browserName) {
  if (browserName !== 'chromium') return null;
  const bytes = await page.evaluate(async () => {
    window.memoryStop = true; await window.memoryDone; return window.memoryMax;
  });
  expect(bytes).toBeGreaterThanOrEqual(64 * 1024 * 1024); expect(bytes).toBeLessThanOrEqual(512 * 1024 * 1024);
  return bytes;
}
function evidence(browserName, suite, data) {
  fs.writeFileSync(path.join(process.env.DW1_WEB_BUILD, `${suite}-${browserName}.json`),
    JSON.stringify({browser:browserName, ...data}, null, 2) + '\n');
}
module.exports = {disc, bin, cue, manifest, launch, entries, memoryStart, memoryEnd, evidence};
