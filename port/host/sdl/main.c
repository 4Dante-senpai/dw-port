/* SDL's main thread owns events, rendering and device lifetime. */
#include "host.h"
#include "../native_cli.h"
#include "../plat.h"
#include <stdio.h>
#include <stdlib.h>

int sdl_headless;
int plat_main_loop(pthread_t game)
{
    if (sdl_headless) { pthread_join(game, NULL); return 0; }
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) exit(0);
            sdl_input_event(&event);
        }
        sdl_input_poll();
        sdl_video_draw();
        SDL_Delay(1);
    }
}
static void shutdown_host(void)
{
    /* Process exit can originate on the game thread; SDL video stays on main.
     * The OS releases video resources at exit. Device teardown is safe here. */
    sdl_audio_quit();
}
int main(int argc, char **argv)
{
    NativeCli options;
    int result = native_cli_parse(argc, argv, &options);
    if (result) return result;
    sdl_headless = options.headless;
    if (!sdl_headless) {
        if (!sdl_video_init()) {
            fprintf(stderr, "dw1: SDL video: %s\n", SDL_GetError());
            return 1;
        }
        if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
            fprintf(stderr, "dw1: gamepad unavailable; continuing: %s\n", SDL_GetError());
        sdl_input_poll();
        sdl_audio_init();
        atexit(shutdown_host);
    }
    result = native_cli_run(&options);
    if (!sdl_headless) { sdl_input_quit(); sdl_video_quit(); sdl_audio_quit(); SDL_Quit(); }
    return result;
}
