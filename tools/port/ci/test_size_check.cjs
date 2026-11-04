'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {brotliCompressSync, constants} = require('node:zlib');

const tool = path.join(__dirname, 'size_check');
const build = fs.mkdtempSync(path.join(os.tmpdir(), 'dw1-size-'));
try {
  // Include/exclude sw.js explicitly to catch both omission and double counting.
  for (const listedSW of [false, true]) for (const version of [false, true]) {
    const listed = ['/', '/dw1.wasm', '/fixture.js'];
    if (listedSW) listed.push('/sw.js');
    const contents = new Map([
      ['dw1.html', Buffer.from('<html>Synthetic shell</html>')],
      ['dw1.wasm', Buffer.from(Array.from({length: 256}, (_, i) => i))],
      ['fixture.js', Buffer.from('const fixture = 42;\n')],
      ['sw.js', Buffer.from(`const FILES = ${JSON.stringify(listed)};\n`)],
    ]);
    if (version) contents.set('version.txt', Buffer.from('synthetic-version\n'));
    else fs.rmSync(path.join(build, 'version.txt'), {force: true});
    const sizes = new Map();
    for (const [name, data] of contents) {
      fs.writeFileSync(path.join(build, name), data);
      sizes.set(name, brotliCompressSync(data, {
        params: {[constants.BROTLI_PARAM_QUALITY]: 11},
      }).length);
    }
    const total = [...sizes.values()].reduce((sum, n) => sum + n, 0);
    const wasm = sizes.get('dw1.wasm');
    function run(wasmLimit, loadLimit, failure) {
      const result = spawnSync(process.execPath, [tool, build,
        '--wasm-limit', String(wasmLimit), '--first-load-limit', String(loadLimit)],
      {encoding: 'utf8'});
      assert.ifError(result.error);
      assert.equal(result.status, failure ? 1 : 0, result.stderr);
      const expected = new Map([...sizes, ['first_load', total]]);
      const printed = result.stdout.trim().split('\n');
      assert.equal(printed.length, expected.size);
      for (const line of printed) {
        const match = /^(.*): brotli_bytes=(\d+)$/.exec(line);
        assert.ok(match, line);
        assert.equal(Number(match[2]), expected.get(match[1]), line);
        expected.delete(match[1]);
      }
      assert.equal(expected.size, 0);
      assert.equal(result.stderr.trim(), failure || '');
    }
    run(wasm, total, null);
    run(wasm - 1, total, `dw1.wasm exceeds --wasm-limit ${wasm - 1}`);
    run(wasm, total - 1, `first load exceeds --first-load-limit ${total - 1}`);
  }
  console.log('size_check_synthetic: OK');
} finally {
  fs.rmSync(build, {recursive: true, force: true});
}
