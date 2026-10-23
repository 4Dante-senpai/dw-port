#ifndef PORT_AUDIO_RATE_H
#define PORT_AUDIO_RATE_H
#include <stdint.h>
#include <stdatomic.h>

#define AUDIO_RATE_CAPACITY 8192u
#define AUDIO_RATE_TARGET 2208u /* Approximately three NTSC VBlanks at 44100 Hz. */
#define AUDIO_RATE_DEADBAND 384u
#define AUDIO_RATE_MAX_ADJUST 0.02 /* covers a device clock off by +/-1% with margin */
#define AUDIO_RATE_GAIN 0.00005   /* ratio change per frame of error past the dead band */

/* One producer (push) and one consumer (pull). Initialize before starting either.
 * Counts are stereo frames. Only the consumer accesses phase, ratio and ramps.
 * Device rate is the declared hardware rate, not an estimate of clock drift. */
typedef struct {
    int16_t pcm[AUDIO_RATE_CAPACITY][2];
    _Atomic uint32_t read, write, underruns, overflows;
    double phase, ratio;
    int started, ramp;
    int16_t last[2];
} AudioRate;
void audio_rate_init(AudioRate *rate);
unsigned audio_rate_push(AudioRate *rate, const int16_t *pcm, unsigned frames);
void audio_rate_pull(AudioRate *rate, int16_t *out, unsigned frames, unsigned device_rate);
unsigned audio_rate_level(const AudioRate *rate);
#endif
