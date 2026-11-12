/* A synthetic live session goes through the real CRT latch and shared recorder. */
#include "../crt.h"
#include "../clock.h"
#include "../playtest.h"
#include "../../backend/gpu.h"
#include <stdlib.h>
#include <string.h>
extern void port_pad_latch(uint16_t mask);
extern uint32_t port_pad_value(void);
void plat_input_vblank(void)
{
    if (!port_playtest_started()) return;
    unsigned k = port_playtest_vblank();
    port_pad_latch(k < 2 ? 8 : k < 5 ? 0x4000 : 0);
}
int dw1_game_main(void)
{
    gpu_reset(1); gpu_set_display(0, 0, 2, 2, 0); gpu_set_display_enable(1);
    clock_advance(clock_vblank_cycle(3));
    port_vsync_return();
    for (;;) {
        gpu_fill(0, 0, 2, 2, port_pad_value() & 255);
        (void)rand();
        clock_advance(clock_vblank_cycle(1));
    }
}
int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    PortCapture config = {.vblank_limit = 10, .digest = strcmp(argv[2], "-") ? argv[2] : NULL,
                          .clock_trace = argv[3], .digest_every = 3};
    if (!strcmp(argv[1], "live")) config.record_inputs = argv[4];
    else config.inputs = argv[4];
    if (!strcmp(argv[5], "off")) config.record_inputs = NULL;
    return port_run_game(0, 0, &config);
}
