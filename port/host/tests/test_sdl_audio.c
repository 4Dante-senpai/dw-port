#include "../sdl/host.h"
#include "../plat.h"
#include "../../backend/spu.h"
#include <assert.h>
#include <stdio.h>

int sdl_headless;
int port_capture_active(void) { return 0; }
static SpuOutput output;
static void *output_arg;
/* Capture exactly the callback registration used by the SPU backend. */
void spu_set_output(SpuOutput fn, void *arg) { output = fn; output_arg = arg; }
int main(void)
{
    sdl_audio_init();
    plat_game_init();
    assert(output);
    assert(sdl_audio_test_level() == 0);
    int16_t pcm[128 * 2];
    for (unsigned i = 0; i < 128; i++) {
        pcm[2 * i] = (int16_t)(1000 + i);
        pcm[2 * i + 1] = (int16_t)(-1000 - (int)i);
    }
    /* Stay below prefill so the live dummy consumer cannot drain the ring. */
    output(pcm, 128, output_arg);
    assert(sdl_audio_test_level() == 128);
    output(pcm, 128, output_arg);
    assert(sdl_audio_test_level() == 256);
    sdl_audio_quit(); SDL_Quit();
    puts("sdl_audio: installed SPU output feeds the SDL audio ring OK");
    return 0;
}
