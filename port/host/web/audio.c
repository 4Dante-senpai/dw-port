/* Single game-thread producer; the AudioWorklet owns the read cursor. */
#include "audio.h"
#include "pace.h"
#include "../audio_rate.h"
#include "../../backend/spu.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <stddef.h>

_Static_assert(offsetof(AudioRate, read) == AUDIO_RATE_CAPACITY * 4, "worklet PCM layout");
_Static_assert(offsetof(AudioRate, overflows) == AUDIO_RATE_CAPACITY * 4 + 12, "worklet atomic layout");
static AudioRate rate;
static int enabled, lossless, ramp;
static uint32_t ramp_frame;
static int wait_timed_out;
static uint32_t timed_out_read;
/* Wall-time diagnostics only; these never change virtual clock accounting. */
static _Atomic uint32_t delivery[4];
static double previous_delivery;
static unsigned previous_level, previous_pauses;
EMSCRIPTEN_KEEPALIVE void *web_audio_address(void) { return &rate; }
EMSCRIPTEN_KEEPALIVE void *web_audio_delivery_address(void) { return delivery; }
EMSCRIPTEN_KEEPALIVE void web_audio_enable(int value)
{
    audio_rate_init(&rate);
    enabled = value;
    wait_timed_out = 0;
}
void web_audio_replay(int value) { lossless = value; }
void web_audio_ramp(int value) { ramp = value; }
static void produce(const int16_t *pcm, unsigned frames, void *arg)
{
    (void)arg;
    _Atomic uint32_t *pause = web_control_address();
    double now = emscripten_get_now();
    if (previous_delivery && !atomic_load(pause) && atomic_load(pause + 4) == previous_pauses) {
        uint32_t gap = (uint32_t)((now - previous_delivery) * 1000);
        if (gap > atomic_load(&delivery[0])) {
            atomic_store(&delivery[0], gap);
            atomic_store(&delivery[1], previous_level);
        }
        if (gap > previous_level * (1000000.0 / 44100)) {
            atomic_store(&delivery[2], gap);
            atomic_store(&delivery[3], previous_level);
        }
    }
    double until = emscripten_get_now() + 100;
    while (audio_rate_level(&rate) > AUDIO_RATE_TARGET + AUDIO_RATE_DEADBAND ||
           (lossless && AUDIO_RATE_CAPACITY - audio_rate_level(&rate) < frames)) {
        if (atomic_load(pause)) {
            web_pace_hold();
            until = emscripten_get_now() + 100;
        } else {
            uint32_t read = atomic_load(&rate.read);
            if (!lossless) {
                if (wait_timed_out && read == timed_out_read) break;
                wait_timed_out = 0;
                if (emscripten_get_now() >= until) {
                    wait_timed_out = 1;
                    timed_out_read = read;
                    break;
                }
            }
            emscripten_futex_wait((void *)&rate.read, read, 1);
        }
    }
    if (ramp) {
        /* A known stereo sequence exposes lost, reordered and duplicated frames. */
        int16_t samples[512 * 2];
        while (frames) {
            unsigned count = frames > 512 ? 512 : frames;
            for (unsigned i = 0; i < count; i++, ramp_frame++) {
                samples[2*i] = (int16_t)(ramp_frame * 31u);
                samples[2*i+1] = (int16_t)(ramp_frame * 17u + 123u);
            }
            audio_rate_push(&rate, samples, count);
            frames -= count;
        }
    } else audio_rate_push(&rate, pcm, frames);
    previous_level = audio_rate_level(&rate);
    previous_pauses = atomic_load(pause + 4);
    previous_delivery = emscripten_get_now();
}
void plat_game_init(void)
{
    if (enabled) spu_set_output(produce, NULL);
}
