#include <stdio.h>
#include <SDL3/SDL.h>

int main(void) {
    int v = SDL_GetVersion();
    if (!SDL_Init(0)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("SDL %d.%d.%d, platform %s, sizeof(void*)=%d\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_GetPlatform(), (int)sizeof(void *));
    SDL_Quit();
    return 0;
}
