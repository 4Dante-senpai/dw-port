#include "host.h"
#include "../plat.h"
#include "../../backend/gpu.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t pixels[2][640 * 480];
static int front, width = 320, height = 240, pending;
static uint64_t deadline, remainder;
static uint64_t monotonic_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
int sdl_video_init(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) return 0;
    window = SDL_CreateWindow("Digimon World", 640, 480, SDL_WINDOW_RESIZABLE);
    if (!window) return 0;
    renderer = SDL_CreateRenderer(window, NULL);
    return renderer != NULL;
}
void plat_native_present(long frame)
{
    (void)frame;
    if (sdl_headless) return;
    /* Only this thread writes back. Main holds the mutex throughout its upload. */
    int back = 1 - front, w = 320, h = 240;
    if (!gpu_read_display(pixels[back], &w, &h)) memset(pixels[back], 0, w*h*4);
    pthread_mutex_lock(&mutex);
    front = back; width = w; height = h; pending = 1;
    pthread_mutex_unlock(&mutex);
    uint64_t now = monotonic_ns();
    if (!deadline || now > deadline + 4 * 16683334u) deadline = now;
    remainder += 1001000000000ull;
    deadline += remainder / 60000; remainder %= 60000;
    struct timespec due = {deadline / 1000000000, deadline % 1000000000};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL) == EINTR) {}
}
void sdl_video_draw(void)
{
    pthread_mutex_lock(&mutex);
    if (pending) {
        float w = 0, h = 0;
        if (texture) SDL_GetTextureSize(texture, &w, &h);
        if ((int)w != width || (int)h != height) {
            SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, width, height);
            if (!texture || !SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST) ||
                !SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
                fprintf(stderr, "dw1: SDL texture: %s\n", SDL_GetError()); exit(1);
            }
        }
        if (!SDL_UpdateTexture(texture, NULL, pixels[front], width * 4) ||
            !SDL_RenderClear(renderer) || !SDL_RenderTexture(renderer, texture, NULL, NULL) ||
            !SDL_RenderPresent(renderer)) {
            fprintf(stderr, "dw1: SDL presentation: %s\n", SDL_GetError()); exit(1);
        }
        pending = 0;
    }
    pthread_mutex_unlock(&mutex);
}
void sdl_video_quit(void)
{
    SDL_DestroyTexture(texture); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
}

#ifdef DW1_SDL_TEST
SDL_Renderer *sdl_video_test_renderer(void) { return renderer; }
#endif
