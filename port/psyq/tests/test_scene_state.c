/* L1: real runtime latching, both origins, reset/reapply, typed state and pointers. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../crt.h"
#include "../clock.h"
#include "../psyq.h"
#include "../ps1mem.h"
#include "../../backend/gpu.h"

int16_t TEST_VALUES[2] = {-32768, 32767};
void *TEST_POINTERS[3];
static int from_mark, checkpoint_mode;
__attribute__((noinline)) void TEST_CHECKPOINT(void) {}
static void check(int condition) { if (!condition) abort(); }
void test(void) {} /* Exported identity for the manually driven mark. */
int dw1_game_main(void)
{
    PadInit(0);
    psyq_libc_reset();
    gpu_reset(1);
    gpu_set_display(0, 0, 1, 1, 0);
    gpu_set_display_enable(1);
    gpu_fill(0, 0, 1, 1, 0xff);
    TEST_POINTERS[0] = Ps1_Ram + 0x123;
    TEST_POINTERS[1] = &TEST_VALUES[1];
    port_vsync_return(); /* first VSync return establishes frame 0 */
    port_frame_done();
    unsigned origin = clock_vblanks();
    check(PadRead(0) == (from_mark ? 0u : 0x800u));
    clock_advance(clock_vblank_cycle(origin + 3) - clock_now());
    port_scene_mark("test");
    if (checkpoint_mode) TEST_CHECKPOINT();
    for (unsigned k = 0; k < 5; k++) {
        rand();
        clock_advance(clock_vblank_cycle(origin + 4 + k) - clock_now());
        unsigned relative = from_mark ? k + 1 : k + 4;
        unsigned expected = relative < 2 ? 0x800 : relative < 4 ? 0x40 : 0;
        if (checkpoint_mode) expected = k == 0 ? 0x800 : 0;
        check(PadRead(0) == expected);
        if (checkpoint_mode && k == 2) TEST_CHECKPOINT();
        if (k == 0) PadInit(1); /* held override must return on the next latch */
    }
    return 1; /* capture exits first */
}
int main(int argc, char **argv)
{
    if (argc != 6 && argc != 7) return 2;
    checkpoint_mode = argc == 7;
    from_mark = !strcmp(argv[5], "mark");
    PortCapture config = {.mark = "test", .vblank = 5, .capture = argv[1],
        .inputs = argv[2], .state_vars = argv[3], .state_trace = argv[4], .inputs_from = argv[5]};
    if (checkpoint_mode) config.checkpoints = argv[6];
    return port_run_game(0, 0, &config);
}
