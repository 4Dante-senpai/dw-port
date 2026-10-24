#include "host.h"
#include "../plat.h"
#include "../audio_rate.h"
#include "../../backend/spu.h"
#include "../../psyq/crt.h"
#include <stdio.h>
static AudioRate rate;
static SDL_AudioStream *stream;
static unsigned device_rate;
static void produce(const int16_t *pcm, unsigned frames, void *arg)
{
    (void)arg;
    /* Virtual time outruns the wall clock while the game loads without presenting: wait for the
     * device to drain instead of dropping samples, bounded in case the device stalls. */
    for (int ms = 0; ms < 100 && !port_capture_active() &&
         audio_rate_level(&rate) > AUDIO_RATE_TARGET + AUDIO_RATE_DEADBAND; ms++)
        SDL_Delay(1);
    audio_rate_push(&rate, pcm, frames);
}
static void SDLCALL consume(void *arg, SDL_AudioStream *audio, int additional, int total)
{
    (void)arg; (void)total;
    int16_t pcm[512 * 2];
    unsigned frames = additional > 0 ? (unsigned)(additional + 3) / 4 : 0;
    while (frames) {
        unsigned count = frames > 512 ? 512 : frames;
        audio_rate_pull(&rate, pcm, count, device_rate);
        if (!SDL_PutAudioStreamData(audio, pcm, count * 4)) break;
        frames -= count;
    }
}
void sdl_audio_init(void)
{
    audio_rate_init(&rate);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) goto fail;
    SDL_AudioSpec spec;
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL)) goto fail;
    device_rate = spec.freq;
    spec.format = SDL_AUDIO_S16; spec.channels = 2;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, consume, NULL);
    if (!stream || !SDL_ResumeAudioStreamDevice(stream)) goto fail;
    return;
fail:
    fprintf(stderr, "dw1: audio unavailable; continuing without audio: %s\n", SDL_GetError());
    SDL_DestroyAudioStream(stream); stream = NULL;
}
void plat_game_init(void)
{
    if (!sdl_headless && stream) spu_set_output(produce, NULL);
}
void sdl_audio_quit(void)
{
    if (!stream) return;
    SDL_DestroyAudioStream(stream); stream = NULL;
    fprintf(stderr, "dw1: audio_rate overflows=%u underruns=%u\n",
            atomic_load(&rate.overflows), atomic_load(&rate.underruns));
}

#ifdef DW1_SDL_TEST
unsigned sdl_audio_test_level(void) { return audio_rate_level(&rate); }
#endif
