/* L1: first mark wins; capture follows callbacks and same-cycle device events. */
#include <stdlib.h>
#include "../clock.h"
#include "../crt.h"
#include "../../backend/gpu.h"
#include "../../backend/cdrom.h"

static uint32_t capture_vblank = 2;

static void paint(void *arg)
{
    (void)arg;
    gpu_fill(0, 0, 1, 1, 0x0000ff);
}
static void vblank(void)
{
    if (clock_vblanks() == capture_vblank) {
        port_scene_mark("target"); /* Must not postpone the first mark. */
        clock_psyq_entry(); /* Also test work deferred beyond the caller's target. */
        clock_schedule(0, paint, NULL);
    }
}
void target(void) {} /* Exported identity for the manually driven mark. */
int dw1_game_main(void)
{
    if (getenv("TEST_PACK")) {
        char err[256];
        if (cdrom_open(getenv("TEST_PACK"), NULL, err, sizeof(err))) return 1;
        for (;;) {
            clock_advance(clock_vblank_cycle(1));
            port_frame_done();
        }
    }
    gpu_reset(1);
    gpu_set_display(0, 0, 1, 1, 0);
    gpu_set_display_enable(!getenv("TEST_DISPLAY_OFF"));
    if (getenv("TEST_MARK_LATE")) {
        clock_advance(clock_vblank_cycle(5));
        capture_vblank = 7;
    }
    clock_set_vblank_handler(vblank);
    port_scene_mark("other");
    port_scene_mark("target");
    if (getenv("TEST_UNTIL_OVERLAY"))
        port_overlay_done();
    clock_advance(clock_vblank_cycle(capture_vblank) - clock_now());
    clock_advance(0);
    return 0; /* Failure if the capture did not terminate at the expected VBlank. */
}
int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    PortCapture config = {.mark = "target", .vblank = 2, .capture = argv[1], .clock_trace = argv[2]};
    return port_run_game(getenv("TEST_PACK") ? 3 : 0,
                         getenv("TEST_UNTIL_OVERLAY") != NULL, &config);
}
