/* Exercise the real VSync(n), SPU and SDL host without copyrighted disc data. */
#include "../../psyq/psyq.h"
#include <stdlib.h>
#include <time.h>
#include "../../backend/gpu.h"
int dw1_game_main(void)
{
    ResetCallback();
    if (getenv("DW1_TEST_GPU_LOAD")) {
        gpu_reset(1);
        static uint16_t texels[256 * 256];
        for (unsigned i = 0; i < 256 * 256; i++) texels[i] = 0x4210;
        gpu_load(0, 0, 256, 256, texels);
    }
    /* Frames of 1, 2 and 3 VBlanks (VSync(0) waits one; VSync(1) would not wait): wall time must follow the virtual VBlanks. */
    for (unsigned iteration = 0;; iteration++) {
        if (getenv("DW1_TEST_GPU_LOAD")) {
            /* Full-screen title-style textured quad followed by the real
             * PsyQ VSync polling loop. No disc or reference pixels needed. */
            const uint32_t quad[] = {0x2c808080, 0, 0, 320, 0x010000ff,
                                     240u << 16, 255u << 8, (240u << 16) | 320, 0xffff};
            const uint32_t fade[] = {0x2a202020, 0, 320, 240u << 16,
                                     (240u << 16) | 320};
            gpu_gp0(quad, 9);
            gpu_gp0(fade, 5);
            VSync(0);
            continue;
        }
        if (iteration == 5 && getenv("DW1_TEST_HOST_STALL")) {
            /* A real host pause exceeds four VBlanks without advancing game time. */
            struct timespec pause = { 0, 150000000 };
            nanosleep(&pause, NULL);
        }
        VSync(0); VSync(2); VSync(3);
    }
    return 0;
}
