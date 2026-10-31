const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const {audioState, audioRing, audioPull} = require('../audio_worklet.js');
const vectors = JSON.parse(execFileSync(process.execPath, [process.argv[2]], {maxBuffer: 8 * 1024 * 1024}));
let ring, state, serial, rate, sawUnderrun = false, sawOverflow = false;
for (const vector of vectors) {
  if (rate !== vector.rate) {
    rate = vector.rate; serial = 0; state = audioState();
    ring = audioRing(new SharedArrayBuffer(8192 * 4 + 16), 0);
    Atomics.store(ring.words, 0, 0xffffffff - 3000);
    Atomics.store(ring.words, 1, 0xffffffff - 3000);
  }
  let write = Atomics.load(ring.words, 1) >>> 0, read = Atomics.load(ring.words, 0) >>> 0;
  const count = Math.min(vector.push, 8192 - ((write - read) >>> 0));
  for (let i = 0; i < vector.push; i++, serial++) if (i < count) {
    const pos = ((write + i) >>> 0) % 8192;
    ring.pcm[pos*2] = serial * 31; ring.pcm[pos*2+1] = serial * 17 + 123;
  }
  Atomics.store(ring.words, 1, write + count);
  Atomics.add(ring.words, 3, vector.push - count);
  const output = new Int16Array(256);
  audioPull(ring, state, output, 128, rate);
  assert.deepEqual(Array.from(output), vector.pcm, `PCM rate=${rate}, serial=${serial}`);
  for (const [i, key] of ['read', 'write', 'underruns', 'overflows'].entries())
    assert.equal(Atomics.load(ring.words, i) >>> 0, vector[key], key);
  for (const key of ['phase', 'ratio', 'started', 'ramp']) assert.equal(state[key], vector[key], key);
  sawUnderrun ||= vector.underruns > 0; sawOverflow ||= vector.overflows > 0;
}
assert(sawUnderrun && sawOverflow);
console.log(`${vectors.length} blocks identical sample by sample at 44100, 48000 and 96000 Hz`);
