const {execSync} = require('child_process');
// Seed OPFS in a temporary dedicated worker; close its exclusive writer before boot.
async function seed(page, file, sha256) {
  await page.goto('/seed.html');
  await page.evaluate(async ({url, sha256}) => {
    await new Promise((resolve, reject) => {
      const worker = new Worker('/seed_worker.js');
      let step = 'worker startup';
      const timer = setTimeout(() => {worker.terminate(); reject(Error('OPFS fixture stalled at ' + step));}, 45000);
      worker.onmessage = ({data}) => {
        if (data.step) {step = data.step; return;}
        if (data.progress !== undefined) {step = 'copy Pack'; return;}
        clearTimeout(timer);
        worker.terminate();
        data.error ? reject(Error(data.error)) : resolve(data.size);
      };
      worker.onerror = e => reject(Error(e.message));
      const address = new URL(url, location.href).href;
      worker.postMessage(sha256 ? {url:address, sha256} : address);
    });
  }, {url:'/__fixture/' + file, sha256});
}
// Resident memory, in MiB, of the browser this test worker launched.
function browserMemory() {
  const rows = execSync('ps -eo pid=,ppid=,rss=').toString().trim().split('\n')
    .map(line => line.trim().split(/\s+/).map(Number));
  const family = new Set([process.pid]);
  for (let size = 0; size !== family.size;) {
    size = family.size;
    for (const [pid, ppid] of rows) if (family.has(ppid)) family.add(pid);
  }
  family.delete(process.pid);
  return rows.reduce((sum, [pid, , rss]) => sum + (family.has(pid) ? rss : 0), 0) / 1024;
}
// IndexedDB "dw1" / "settings" / "port_settings", read and written from a page without the Shell.
function store(page, value) {
  return page.evaluate(value => new Promise((resolve, reject) => {
    const open = indexedDB.open('dw1');
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
function errors(page) {
  const result = [];
  page.on('pageerror', e => result.push(String(e)));
  page.on('console', m => {if (/abort|RuntimeError|forbidden|signature mismatch/i.test(m.text())) result.push(m.text());});
  page.on('dialog', async d => {result.push(d.message()); await d.dismiss();});
  return result;
}
// Context requests include dedicated workers. Chromium omits AudioWorklet loads
// (including imports), so supplement them with browser-wide CDP Fetch events.
async function foreignRequests(page) {
  const result = [], origin = new URL(process.env.DW1_WEB_URL).origin;
  const counts = new Map();
  const record = (address, source) => {
    const url = new URL(address);
    if (['data:', 'blob:'].includes(url.protocol) || url.origin === origin) return;
    const count = counts.get(address) || {context:0, browser:0};
    const before = Math.max(count.context, count.browser);
    ++count[source];
    if (Math.max(count.context, count.browser) > before) result.push(address);
    counts.set(address, count);
  };
  page.context().on('request', request => record(request.url(), 'context'));
  if (page.context().browser().browserType().name() === 'chromium') {
    const session = await page.context().browser().newBrowserCDPSession();
    session.on('Fetch.requestPaused', async event => {
      record(event.request.url, 'browser');
      try {
        await session.send('Fetch.continueRequest', {requestId:event.requestId});
      } catch (error) {
        if (!page.isClosed()) result.push('network observer error: ' + error.message);
      }
    });
    await session.send('Fetch.enable', {patterns:[{urlPattern:'*'}]});
    page.on('close', () => session.detach().catch(() => {}));
  }
  return result;
}
module.exports = {seed, errors, browserMemory, store, foreignRequests};
