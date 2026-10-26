/* L1: checkpoint boundaries exclude the gap and reset both audio origins. */
#include "../crt.h"
#include "../scene.h"
#include "../clock.h"
#include "../../backend/spu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void AUDIO_START(void) {}
void AUDIO_FINISH(void) {}
void AUDIO_MUSIC(void) {}
void AUDIO_STOP(void) {}
void AUDIO_LANDING(void) {}

int dw1_game_main(void)
{
    port_frame_done();
    uint64_t first_cycle = clock_now();
    port_scene_enter(AUDIO_START);
    port_scene_enter(AUDIO_MUSIC); /* Scoped anchor is not armed yet. */
    spu_write16(0x180, 0x1234);
    clock_advance(clock_vblank_cycle(1) - clock_now());
    spu_write16(0x182, 0x2345);
    clock_advance(clock_vblank_cycle(2) - clock_now());
    port_scene_enter(AUDIO_FINISH);
    spu_write16(0x180, 0xdead); /* Outside both segments. */
    clock_advance(clock_vblank_cycle(5) + 17 - clock_now());
    port_scene_enter(AUDIO_MUSIC);
    uint64_t music_cycle = clock_now();
    spu_write16(0x180, 0x3456);
    if (getenv("DW1_TEST_NOTE_ANCHOR")) {
        unsigned base = !strcmp(getenv("DW1_TEST_NOTE_ANCHOR"), "high") ? 0x110 : 0;
        spu_write16(0, 0xdead); /* Inherited volume is not a new note. */
        spu_write16(0x188, 0); spu_write16(0x18a, 0);
        spu_write16(0x16, 0x1234); /* Address and KON refer to different voices. */
        spu_write16(0x188, 1); spu_write16(0x18a, 0);
        clock_advance(821);
        music_cycle = clock_now();
        spu_write16(base + 4, 0x400); /* Origin precedes its sample address/KON. */
        clock_advance(800);
        spu_write16(base, 0x123); spu_write16(base + 2, 0x234);
        spu_write16(base + 6, 0x345); spu_write16(base + 8, 0x456); spu_write16(base + 10, 0x567);
        spu_write16(0x188, base ? 0 : 1); spu_write16(0x18a, base ? 2 : 0);
    }
    clock_advance(clock_vblank_cycle(6) - clock_now());
    spu_write16(0x182, 0x4567);
    printf("frames %llu %llu\n", (unsigned long long)(clock_vblank_cycle(2) / 768 - first_cycle / 768),
           (unsigned long long)(clock_vblank_cycle(7) / 768 - music_cycle / 768));
    clock_advance(clock_vblank_cycle(7) - clock_now());
    port_scene_enter(AUDIO_STOP);
    spu_write16(0x180, 0xdead); /* First game event is excluded. */
    port_scene_enter(AUDIO_LANDING);
    clock_advance(clock_vblank_cycle(8) - clock_now());
    abort(); /* Successful capture exits from the VBlank observer. */
}

int main(int argc, char **argv)
{
    if (argc != 3) return 2;
    PortCapture capture = {.mark = "AUDIO_START", .checkpoints = argv[1], .audio_segments = argv[2]};
    return port_run_game(0, 0, &capture);
}
