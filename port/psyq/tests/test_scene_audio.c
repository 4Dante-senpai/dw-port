/* L1: checkpoint boundaries exclude the gap and reset both audio origins. */
#include "../crt.h"
#include "../scene.h"
#include "../clock.h"
#include "../../backend/spu.h"
#include <stdio.h>
#include <stdlib.h>

void AUDIO_START(void) {}
void AUDIO_FINISH(void) {}
void AUDIO_MUSIC(void) {}

int dw1_game_main(void)
{
    port_frame_done();
    port_scene_enter(AUDIO_START);
    spu_write16(0x180, 0x1234);
    clock_advance(clock_vblank_cycle(1) - clock_now());
    spu_write16(0x182, 0x2345);
    clock_advance(clock_vblank_cycle(2) - clock_now());
    port_scene_enter(AUDIO_FINISH);
    spu_write16(0x180, 0xdead); /* Outside both segments. */
    clock_advance(clock_vblank_cycle(5) + 17 - clock_now());
    port_scene_enter(AUDIO_MUSIC);
    spu_write16(0x180, 0x3456);
    clock_advance(clock_vblank_cycle(6) - clock_now());
    spu_write16(0x182, 0x4567);
    printf("frames %llu %llu\n", (unsigned long long)(clock_vblank_cycle(2) / 768),
           (unsigned long long)(clock_vblank_cycle(7) / 768 - (clock_vblank_cycle(5) + 17) / 768));
    clock_advance(clock_vblank_cycle(7) - clock_now());
    abort(); /* Successful capture exits from the VBlank observer. */
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    PortCapture capture = {.mark = "AUDIO_START", .checkpoints = argv[1], .audio_segments = argv[2]};
    return port_run_game(0, 0, &capture);
}
