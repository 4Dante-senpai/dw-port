/* Deterministic streaming vectors: startup, drift, wrap, starvation and recovery. */
#include "../audio_rate.h"
#include <stdio.h>
int main(void)
{
    const unsigned rates[] = {44100, 48000, 96000};
    puts("[");
    for (unsigned rate = 0; rate < 3; rate++) {
        AudioRate ring;
        audio_rate_init(&ring);
        unsigned serial = 0;
        /* Exercise the uint32 cursor rollover as well as physical ring wrap. */
        atomic_store(&ring.read, UINT32_MAX - 3000);
        atomic_store(&ring.write, UINT32_MAX - 3000);
        for (unsigned block = 0; block < 360; block++) {
            unsigned count = block < 6 ? 600 : block < 90 ? (block % 5 ? 135 : 400) : block < 300 ? 0 : 700;
            int16_t input[1400], output[256];
            for (unsigned i = 0; i < count; i++, serial++) {
                input[2*i] = (int16_t)(serial * 31u);
                input[2*i+1] = (int16_t)(serial * 17u + 123u);
            }
            audio_rate_push(&ring, input, count);
            audio_rate_pull(&ring, output, 128, rates[rate]);
            printf("%s{\"rate\":%u,\"push\":%u,\"read\":%u,\"write\":%u,\"underruns\":%u,\"overflows\":%u,\"pcm\":[",
                   rate || block ? "," : "", rates[rate], count,
                   atomic_load(&ring.read), atomic_load(&ring.write),
                   atomic_load(&ring.underruns), atomic_load(&ring.overflows));
            for (unsigned i = 0; i < 256; i++) printf("%s%d", i ? "," : "", output[i]);
            printf("],\"phase\":%.17g,\"ratio\":%.17g,\"started\":%d,\"ramp\":%d}\n",
                   ring.phase, ring.ratio, ring.started, ring.ramp);
        }
    }
    puts("]");
    return 0;
}
