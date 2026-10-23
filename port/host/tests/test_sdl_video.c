#include "../sdl/host.h"
#include "../plat.h"
#include "../../backend/gpu.h"
#include <assert.h>
#include <stdio.h>

int sdl_headless;
uint32_t clock_vblanks(void) { return 0; }
int port_capture_active(void) { return 0; }
static void pixel(SDL_Surface *surface, int x, int y, Uint8 red, Uint8 green, Uint8 blue)
{
    Uint8 r, g, b, a;
    assert(SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a));
    assert(r == red && g == green && b == blue);
}
int main(void)
{
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    assert(sdl_video_init());
    gpu_reset(1);
    /* Distinct rows and isolated colors expose channel swaps and upload pitch errors. */
    static uint16_t pattern[320 * 240];
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++)
            pattern[y * 320 + x] = y < 120 ? (x < 160 ? 31 : 31 << 5) :
                                                          (x < 160 ? 31 << 10 : 0x7fff);
    pattern[73 * 320 + 29] = 31 | (31 << 10);
    gpu_load(16, 8, 320, 240, pattern);
    gpu_set_display(16, 8, 320, 240, 0);
    gpu_set_display_enable(1);
    plat_native_present(0);
    sdl_video_draw();
    SDL_Surface *surface = SDL_RenderReadPixels(sdl_video_test_renderer(), NULL);
    assert(surface && surface->w == 640 && surface->h == 480);
    pixel(surface, 0, 0, 255, 0, 0);
    pixel(surface, 639, 0, 0, 255, 0);
    pixel(surface, 0, 479, 0, 0, 255);
    pixel(surface, 639, 479, 255, 255, 255);
    pixel(surface, 58, 146, 255, 0, 255);
    pixel(surface, 57, 146, 255, 0, 0);
    pixel(surface, 58, 144, 255, 0, 0);
    SDL_DestroySurface(surface);
    sdl_video_quit(); SDL_Quit();
    puts("sdl_video: presented RGB channels, pitch and nearest scaling OK");
    return 0;
}
