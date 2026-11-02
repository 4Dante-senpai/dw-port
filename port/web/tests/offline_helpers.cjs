const fs = require('fs');
const path = require('path');
const {spawn, execFileSync} = require('child_process');
const {expect} = require('/opt/node_modules/@playwright/test');
const h = require('./disc_helpers.cjs');
const root = path.resolve(__dirname, '../../..');
const files = ['dw1.html', 'dw1.js', 'dw1.wasm', 'io_worker.js', 'audio_worklet.js',
  'dw1_pack.js', 'dw1_pack.wasm', 'us.tsv', 'sw.js', 'seed.html', 'seed_worker.js', 'disc_hash_worker.js'];
class Site {
  constructor(project, real = false) {
    this.dir = fs.mkdtempSync(path.join(process.env.DW1_WEB_BUILD, '.offline-' + project + '-'));
    this.real = real;
    for (const file of files) fs.copyFileSync(path.join(process.env.DW1_WEB_BUILD, file), path.join(this.dir, file));
    if (real) {
      for (const file of ['us.pack', 'us.pack.sha256'])
        fs.linkSync(path.join(process.env.DW1_WEB_BUILD, file), path.join(this.dir, file));
    }
    this.publish('one');
  }
  publish(version) {
    for (const file of ['dw1.html', 'dw1.js', 'io_worker.js', 'audio_worklet.js']) {
      const marker = 'DW1 offline test version: ' + version;
      const comment = file.endsWith('.html') ? `<!-- ${marker} -->` : `// ${marker}`;
      fs.writeFileSync(path.join(this.dir, file),
        fs.readFileSync(path.join(process.env.DW1_WEB_BUILD, file), 'utf8') + '\n' + comment + '\n');
    }
    fs.writeFileSync(path.join(this.dir, 'version.txt'), version + '\n');
    execFileSync('python3', [path.join(root, 'tools/port/web/stamp_sw.py'), this.dir]);
    return fs.readFileSync(path.join(this.dir, 'sw.js'), 'utf8');
  }
  async start() {
    if (this.server) return;
    this.server = spawn('python3', [path.join(root, 'port/web/dev/serve.py'),
      '--build', this.dir, '--port', new URL(process.env.DW1_WEB_URL).port],
      {env:{...process.env, DW1_OFFLINE_TEST_SERVER:'1'}, stdio:['ignore', 'ignore', 'pipe']});
    this.server.stderr.on('data', data => { this.log = (this.log || '') + data; });
    await expect.poll(async () => {
      try { return (await fetch(process.env.DW1_WEB_URL + '/version.txt')).ok; } catch (_) { return false; }
    }).toBe(true);
  }
  async prepare(page) {
    if (this.real) return;
    fs.writeFileSync(path.join(this.dir, 'us.tsv'), await h.manifest(page, h.disc()));
    this.publish('one');
  }
  async stop() {
    if (!this.server) return;
    const child = this.server; this.server = null;
    await new Promise(resolve => { child.once('exit', resolve); child.kill('SIGTERM'); });
  }
  async close() { await this.stop(); fs.rmSync(this.dir, {recursive:true, force:true}); }
  fail(url) { fs.writeFileSync(path.join(this.dir, 'fail'), url); }
  gate() { fs.writeFileSync(path.join(this.dir, 'gate'), ''); }
  release() { fs.rmSync(path.join(this.dir, 'gate'), {force:true}); }
}
async function controlled(page) {
  await page.evaluate(async () => {
    await navigator.serviceWorker.ready;
    if (!navigator.serviceWorker.controller) await new Promise(resolve =>
      navigator.serviceWorker.addEventListener('controllerchange', resolve, {once:true}));
  });
}
async function ready(page, text = 'Listo para jugar') {
  expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);
  await expect(page.locator('#instance')).toBeHidden();
  await expect(page.locator('#pack-status')).toHaveText(text);
  await expect(page.locator('#status')).toHaveText('');
  await expect(page.locator('#play')).toBeEnabled({timeout:5000});
}
async function build(page) {
  await page.goto('/');
  await expect(page.locator('#disc')).toBeEnabled();
  await page.locator('#disc').setInputFiles([h.bin(h.disc())]);
  await ready(page);
  await controlled(page);
}
async function opfs(page, operation, value) {
  return page.evaluate(async ({operation, value}) => new Promise((resolve, reject) => {
    const source = `onmessage = async ({data}) => { try {
      const dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('dw1');
      if (data.operation === 'read') {
        const pack = await (await dir.getFileHandle('us.pack')).getFile();
        postMessage([...new Uint8Array(await pack.arrayBuffer())]); return;
      }
      if (data.operation === 'version' || data.operation === 'corrupt') {
        const handle = await (await dir.getFileHandle('us.pack')).createSyncAccessHandle();
        try {
          const bytes = new Uint8Array(4); new DataView(bytes.buffer).setUint32(0, data.value, true);
          handle.write(bytes, {at:data.operation === 'version' ? 8 : 2048}); handle.flush();
        } finally { handle.close(); }
      } else if (data.operation === 'previous') {
        await (await dir.getFileHandle('us.pack')).move('us.pack.previous');
      } else if (data.operation === 'leftovers') {
        for (const name of ['us.pack.previous', 'us.pack.tmp']) {
          const handle = await (await dir.getFileHandle(name, {create:true})).createSyncAccessHandle(); handle.close();
        }
      }
      postMessage(true);
    } catch (e) { postMessage({error:e.message}); } };`;
    const url = URL.createObjectURL(new Blob([source], {type:'text/javascript'}));
    const worker = new Worker(url);
    worker.onmessage = ({data}) => { worker.terminate(); URL.revokeObjectURL(url); data.error ? reject(Error(data.error)) : resolve(data); };
    worker.onerror = e => reject(Error(e.message));
    worker.postMessage({operation, value});
  }), {operation, value});
}
async function waiting(page) {
  await expect.poll(() => page.evaluate(async () => !!(await navigator.serviceWorker.getRegistration()).waiting), {timeout:30000}).toBe(true);
}
async function cachesAt(page) { return page.evaluate(() => caches.keys()); }
module.exports = {Site, controlled, ready, build, opfs, waiting, cachesAt};
