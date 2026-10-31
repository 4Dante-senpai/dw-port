/* Host resampler: same integer rounding and state transitions as audio_rate_pull.
 * CommonJS exports allow Node to compare it against C without a browser or wasm here. */
const CAPACITY = 8192, TARGET = 6144, DEADBAND = 1536, MAX_ADJUST = 0.02, GAIN = 0.00005;
function audioState() { return {phase: 0, ratio: 1, started: 0, ramp: 0, last: [0, 0]}; }
function audioRing(memory, address) {
  return {pcm: new Int16Array(memory, address, CAPACITY * 2),
    words: new Int32Array(memory, address + CAPACITY * 4, 4)};
}
function audioPull(ring, state, out, frames, deviceRate, tap) {
  if (!deviceRate) { out.fill(0); return; }
  const {pcm, words} = ring;
  let read = Atomics.load(words, 0) >>> 0;
  const initial = read, write = Atomics.load(words, 1) >>> 0;
  const level = () => (write - read) >>> 0;
  const error = level() - TARGET;
  let adjust = 0;
  if (error > DEADBAND) adjust = (error - DEADBAND) * GAIN;
  if (error < -DEADBAND) adjust = (error + DEADBAND) * GAIN;
  state.ratio = 1 + Math.max(-MAX_ADJUST, Math.min(MAX_ADJUST, adjust));
  const step = 44100 / deviceRate * state.ratio;
  for (let i = 0; i < frames; i++) {
    if (!state.started && level() >= TARGET) {
      state.started = 1; state.ramp = 64; state.phase = 0;
    }
    const advance = Math.trunc(state.phase + step);
    if (state.started && (level() < 2 || advance > level())) {
      state.started = 0; state.ramp = 0;
      Atomics.add(words, 2, 1);
    }
    for (let ch = 0; ch < 2; ch++) {
      let value;
      if (state.started) {
        const a = pcm[(read % CAPACITY) * 2 + ch], b = pcm[((read + 1) % CAPACITY) * 2 + ch];
        value = Math.trunc(a + (b - a) * state.phase);
        if (state.ramp) value = Math.trunc(value * (64 - state.ramp) / 64);
      } else {
        value = state.last[ch];
        value = value > 512 ? value - 512 : value < -512 ? value + 512 : 0;
      }
      out[2*i + ch] = state.last[ch] = value;
    }
    if (state.started) {
      if (state.ramp) state.ramp--;
      state.phase += step; state.phase -= advance; read = (read + advance) >>> 0;
    }
  }
  // Copy raw consumed frames before releasing their slots to the producer.
  if (tap && read !== initial) {
    const raw = new Int16Array(((read - initial) >>> 0) * 2);
    for (let i = 0; i < raw.length / 2; i++) {
      const pos = ((initial + i) >>> 0) % CAPACITY;
      raw[2*i] = pcm[2*pos]; raw[2*i+1] = pcm[2*pos+1];
    }
    tap(raw);
  }
  Atomics.store(words, 0, read);
  Atomics.notify(words, 0);
}
if (typeof module !== 'undefined') module.exports = {audioState, audioRing, audioPull};
if (typeof registerProcessor === 'function') {
  class DW1Audio extends AudioWorkletProcessor {
    constructor({processorOptions: options}) {
      super();
      this.ring = audioRing(options.memory, options.address);
      this.pause = new Int32Array(options.memory, options.control, 1);
      this.state = audioState();
      this.out = new Int16Array(256);
      this.tap = options.tap ? raw => this.port.postMessage(raw, [raw.buffer]) : null;
      this.ready = null;
      this.port.onmessage = ({data}) => { if (data.ready !== undefined) this.ready = data.ready; };
    }
    process(inputs, outputs) {
      const channels = outputs[0], frames = channels[0].length;
      // A running AudioContext can precede the device's first render callback.
      if (this.ready !== null) { this.port.postMessage({ready: this.ready}); this.ready = null; }
      if (Atomics.load(this.pause, 0)) { for (const ch of channels) ch.fill(0); return true; }
      if (this.out.length !== frames * 2) this.out = new Int16Array(frames * 2);
      audioPull(this.ring, this.state, this.out, frames, sampleRate, this.tap);
      for (let i = 0; i < frames; i++) {
        channels[0][i] = this.out[2*i] / 32768;
        channels[1][i] = this.out[2*i+1] / 32768;
      }
      return true;
    }
  }
  registerProcessor('dw1-audio', DW1Audio);
}
