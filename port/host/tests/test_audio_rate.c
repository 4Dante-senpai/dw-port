#include "../audio_rate.h"
#include "../../psyq/clock.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AudioRate rate;
static int16_t pcm[1024 * 2], output[512 * 2];

/* Exact rational producer at VBlank boundaries; independent callback schedule.
 * Device frequency and an unreported oscillator error are separate inputs. */
static void simulate(unsigned hz, unsigned speed)
{
    audio_rate_init(&rate);
    assert(atomic_is_lock_free(&rate.read) && atomic_is_lock_free(&rate.write));
    uint64_t produced = 0, callbacks = 0;
    unsigned low = AUDIO_RATE_CAPACITY, high = 0;
    for (uint64_t vb = 1; vb <= 36000; vb++) {
        uint64_t until = vb * 44100 * CLOCK_VBLANK_NUM / CLOCK_VBLANK_DEN;
        unsigned count = (unsigned)(until - produced);
        for (unsigned i = 0; i < count; i++) {
            pcm[2*i] = (int16_t)(((int)((produced + i) % 256) - 128) * 90);
            pcm[2*i+1] = -pcm[2*i];
        }
        assert(audio_rate_push(&rate, pcm, count) == count);
        produced = until;
        /* Simulate a declared hardware frequency, optionally with hidden drift. */
        while ((callbacks + 1) * 256 * CLOCK_VBLANK_DEN * 10000ull <= vb * CLOCK_VBLANK_NUM * hz * speed) {
            audio_rate_pull(&rate, output, 256, hz);
            for (unsigned i = 0; i < 256; i++) assert(abs(output[2*i] + output[2*i+1]) <= 1);
            assert(fabs(rate.ratio - 1) <= AUDIO_RATE_MAX_ADJUST + 1e-12);
            callbacks++;
            if (vb > 60) {
                unsigned level = audio_rate_level(&rate);
                if (level < low) low = level;
                if (level > high) high = level;
                if (level < 735 || level > 3680) fprintf(stderr, "hz=%u speed=%u vb=%llu level=%u ratio=%g\n", hz, speed, (unsigned long long)vb, level, rate.ratio);
                assert(level >= 735 && level <= 3680);
            }
        }
    }
    assert(atomic_load(&rate.underruns) == 0 && atomic_load(&rate.overflows) == 0);
    printf("audio_rate: 36000 VBlanks, device=%u Hz, clock=%u/10000, level=%u..%u, underruns=0\n", hz, speed, low, high);
}

static void edges(void)
{
    audio_rate_init(&rate);
    audio_rate_pull(&rate, output, 512, 48000);
    for (unsigned i = 0; i < 1024; i++) assert(output[i] == 0);
    assert(!atomic_load(&rate.underruns));
    for (unsigned i = 0; i < 2048; i++) pcm[i] = 16000;
    for (unsigned i = 0; i < 3; i++) assert(audio_rate_push(&rate, pcm, 1024) == 1024);
    for (unsigned i = 0; i < 10; i++) audio_rate_pull(&rate, output, 512, 48000);
    assert(atomic_load(&rate.underruns) == 1);
    for (unsigned i = 0; i < 1024; i++) assert(output[i] == 0);
    /* Refill recovers without a discontinuity at the first sample. */
    for (unsigned i = 0; i < 3; i++) audio_rate_push(&rate, pcm, 1024);
    audio_rate_pull(&rate, output, 512, 48000);
    assert(output[0] == 0 && output[128] == 16000);
    audio_rate_init(&rate);
    for (unsigned i = 0; i < 8; i++) assert(audio_rate_push(&rate, pcm, 1024) == 1024);
    assert(audio_rate_push(&rate, pcm, 1) == 0 && atomic_load(&rate.overflows) == 1);
    audio_rate_pull(&rate, output, 1, 48000);
    assert(rate.ratio == 1 + AUDIO_RATE_MAX_ADJUST);
    audio_rate_init(&rate);
    atomic_store(&rate.read, UINT32_MAX - 1000); atomic_store(&rate.write, UINT32_MAX - 1000);
    for (unsigned i = 0; i < 3; i++) audio_rate_push(&rate, pcm, 1024);
    audio_rate_pull(&rate, output, 512, 44100);
    assert(audio_rate_level(&rate) < 3072 && output[128] == 16000);
}

static void interpolation(void)
{
    audio_rate_init(&rate);
    for (unsigned i = 0; i < 1024; i++) { pcm[2*i] = (i % 100)*100; pcm[2*i+1] = -pcm[2*i]; }
    audio_rate_push(&rate, pcm, 1024); audio_rate_push(&rate, pcm, 1024);
    audio_rate_push(&rate, pcm, AUDIO_RATE_TARGET - 2048);
    audio_rate_pull(&rate, output, 128, 88200);
    assert(rate.ratio == 1.0);
    for (unsigned i = 64; i < 128; i++) {
        assert(output[2*i] == (int)i * 50);
        assert(output[2*i+1] == -(int)i * 50);
    }
    assert(audio_rate_level(&rate) == AUDIO_RATE_TARGET - 64);
}

int main(void)
{
    edges();
    interpolation();
    simulate(48000, 10000);
    simulate(48480, 10000); simulate(47520, 10000);
    simulate(48000, 10100); simulate(48000, 9900);
    puts("audio_rate: OK");
    return 0;
}
