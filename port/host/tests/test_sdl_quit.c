/* Exercise the actual main-loop hook with a synthetic game that asks to quit. */
#define main sdl_host_main
#include "../sdl/main.c"
#undef main
#include <unistd.h>
int dw1_game_main(void)
{
    SDL_Event event = {.type = SDL_EVENT_QUIT};
    if (!SDL_PushEvent(&event)) exit(1);
    for (;;) pause();
}
int main(int argc, char **argv) { return sdl_host_main(argc, argv); }
