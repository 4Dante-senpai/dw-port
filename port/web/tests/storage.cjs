// Seed OPFS in a temporary dedicated worker; close its exclusive writer before boot.
async function seed(page, file) {
  await page.goto('/seed.html');
  await page.evaluate(async url => {
    await new Promise((resolve, reject) => {
      const worker = new Worker('/seed_worker.js');
      let step = 'worker startup';
      const timer = setTimeout(() => {worker.terminate(); reject(Error('OPFS fixture stalled at ' + step));}, 45000);
      worker.onmessage = ({data}) => {
        if (data.step) {step = data.step; return;}
        clearTimeout(timer);
        worker.terminate();
        data.error ? reject(Error(data.error)) : resolve(data.size);
      };
      worker.onerror = e => reject(Error(e.message));
      worker.postMessage(new URL(url, location.href).href);
    });
  }, '/__fixture/' + file);
}
function errors(page) {
  const result = [];
  page.on('pageerror', e => result.push(String(e)));
  page.on('console', m => {if (/abort|RuntimeError|forbidden|signature mismatch/i.test(m.text())) result.push(m.text());});
  page.on('dialog', async d => {result.push(d.message()); await d.dismiss();});
  return result;
}
module.exports = {seed, errors};
