/* Host-only drift correction. Never changes the SPU or virtual clock. */
#include "audio_rate.h"
#include <string.h>

void audio_rate_init(AudioRate *r)
{
    memset(r, 0, sizeof(*r));
    atomic_init(&r->read, 0); atomic_init(&r->write, 0);
    atomic_init(&r->underruns, 0); atomic_init(&r->overflows, 0);
    r->ratio = 1.0;
}
unsigned audio_rate_level(const AudioRate *r)
{
    uint32_t read = atomic_load_explicit(&r->read, memory_order_acquire);
    return atomic_load_explicit(&r->write, memory_order_acquire) - read;
}
unsigned audio_rate_push(AudioRate *r, const int16_t *pcm, unsigned frames)
{
    uint32_t write = atomic_load_explicit(&r->write, memory_order_relaxed);
    unsigned free = AUDIO_RATE_CAPACITY - (write - atomic_load_explicit(&r->read, memory_order_acquire));
    unsigned count = frames < free ? frames : free;
    for (unsigned i = 0; i < count; i++)
        memcpy(r->pcm[(write + i) % AUDIO_RATE_CAPACITY], pcm + 2*i, 2*sizeof(int16_t));
    atomic_store_explicit(&r->write, write + count, memory_order_release);
    if (count < frames) atomic_fetch_add_explicit(&r->overflows, frames - count, memory_order_relaxed);
    return count;
}
void audio_rate_pull(AudioRate *r, int16_t *out, unsigned frames, unsigned device_rate)
{
    if (!device_rate) { memset(out, 0, frames * 2 * sizeof(*out)); return; }
    uint32_t read = atomic_load_explicit(&r->read, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&r->write, memory_order_acquire);
    int error = (int)(write - read) - AUDIO_RATE_TARGET;
    double adjust = 0;
    if (error > (int)AUDIO_RATE_DEADBAND) adjust = (error - AUDIO_RATE_DEADBAND) * AUDIO_RATE_GAIN;
    if (error < -(int)AUDIO_RATE_DEADBAND) adjust = (error + (int)AUDIO_RATE_DEADBAND) * AUDIO_RATE_GAIN;
    if (adjust > AUDIO_RATE_MAX_ADJUST) adjust = AUDIO_RATE_MAX_ADJUST;
    if (adjust < -AUDIO_RATE_MAX_ADJUST) adjust = -AUDIO_RATE_MAX_ADJUST;
    r->ratio = 1 + adjust;
    double step = 44100.0 / device_rate * r->ratio;
    for (unsigned i = 0; i < frames; i++) {
        if (!r->started && write - read >= AUDIO_RATE_TARGET) {
            r->started = 1; r->ramp = 64; r->phase = 0;
        }
        unsigned advance = (unsigned)(r->phase + step);
        if (r->started && (write - read < 2 || advance > write - read)) {
            r->started = 0; r->ramp = 0;
            atomic_fetch_add_explicit(&r->underruns, 1, memory_order_relaxed);
        }
        for (unsigned ch = 0; ch < 2; ch++) {
            int value;
            if (r->started) {
                int a = r->pcm[read % AUDIO_RATE_CAPACITY][ch];
                int b = r->pcm[(read + 1) % AUDIO_RATE_CAPACITY][ch];
                value = (int)(a + (b - a) * r->phase);
                if (r->ramp) value = value * (64 - r->ramp) / 64;
            } else {
                /* At most 512 units per sample: a bounded fade to silence. */
                value = r->last[ch];
                value = value > 512 ? value - 512 : value < -512 ? value + 512 : 0;
            }
            out[2*i + ch] = r->last[ch] = (int16_t)value;
        }
        if (r->started) {
            if (r->ramp) r->ramp--;
            r->phase += step; r->phase -= advance; read += advance;
        }
    }
    atomic_store_explicit(&r->read, read, memory_order_release);
}
