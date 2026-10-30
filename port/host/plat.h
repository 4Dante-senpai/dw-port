#ifndef PORT_PLAT_H
#define PORT_PLAT_H
#include <stdint.h>
#ifdef __EMSCRIPTEN__
void plat_present(long frame);
/* Applies the Shell's sampled input at each VBlank (port/host/web/input.c). */
void plat_input_vblank(void);
#else
#include <pthread.h>
/* Optional native host hooks; headless has no platform dependencies. */
void plat_native_present(long frame) __attribute__((weak));
void plat_game_init(void) __attribute__((weak));
void plat_input_vblank(void) __attribute__((weak));
void plat_native_wall_begin(void) __attribute__((weak));
void plat_native_wall_finish(void) __attribute__((weak));
int plat_main_loop(pthread_t game) __attribute__((weak));
static inline void plat_present(long frame)
{
#ifdef DW1_HOST_SDL
    if (plat_native_present) plat_native_present(frame);
#else
    (void)frame;
#endif
}
#endif
#endif
