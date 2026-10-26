#include "host.h"
#include "../plat.h"
#include "../../backend/gpu.h"
#include "../../psyq/clock.h"
#include "../../psyq/crt.h"
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
static uint32_t last_vblank;
static uint64_t anchor;
static uint32_t anchor_vblank, resynchronizations;
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
static void pace(void)
{
    if (sdl_headless || port_capture_active()) return;
    uint32_t vblank = clock_vblanks();
    uint32_t elapsed = vblank - last_vblank;
    last_vblank = vblank;
    uint64_t now = monotonic_ns();
    /* Frame 0 is the first VSync return, including nonblocking queries. */
    if (!deadline) { anchor = deadline = now; anchor_vblank = vblank; return; }
    remainder += (uint64_t)elapsed * CLOCK_VBLANK_NUM * 1000000000ull;
    deadline += remainder / CLOCK_VBLANK_DEN; remainder %= CLOCK_VBLANK_DEN;
    /* Drop a missed deadline rather than catch up after a debugger pause. */
    if (now > deadline && now - deadline > 4ull * CLOCK_VBLANK_NUM * 1000000000ull / CLOCK_VBLANK_DEN) {
        resynchronizations++;
        fprintf(stderr, "dw1: pacing resynchronization vblank=%u late_ns=%llu\n",
                vblank - anchor_vblank, (unsigned long long)(now - deadline));
        deadline = now;
        remainder = 0;
    }
    struct timespec due = {deadline / 1000000000, deadline % 1000000000};
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, NULL) == EINTR) {}
}
void plat_native_wall_begin(void)
{
    pace();
}
void plat_native_wall_finish(void)
{
    pace();
    if (anchor) printf("port: wall vblanks=%u nanoseconds=%llu resynchronizations=%u rate_num=%u rate_den=%u\n",
                       clock_vblanks() - anchor_vblank,
                       (unsigned long long)(monotonic_ns() - anchor), resynchronizations,
                       CLOCK_VBLANK_DEN, CLOCK_VBLANK_NUM);
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
    pace();
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
