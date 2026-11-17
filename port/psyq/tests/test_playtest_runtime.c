/* A synthetic live session goes through the real CRT latch and shared recorder. */
#define LANGUAGE_C
#include <dw/entity.h>
#include <dw/params.h>
#include <dw/script.h>
#include <dw/btl.h>
#include <dw/clock.h>
#include "../crt.h"
#include "../clock.h"
#include "../playtest.h"
#include "../death.h"
#include "../port.h"
#include <fcntl.h>
#include <signal.h>
#include "../../backend/gpu.h"
#include <stdlib.h>
#include <string.h>
extern void port_pad_latch(uint16_t mask);
extern uint32_t port_pad_value(void);
/* Globals the state channel reads (port/psyq/state.c). screen and money change every VBlank; the
 * rest hold pairwise-distinct constants so a mis-wired record field shows up in the tests. */
uint16_t CURRENT_SCREEN_ID;
int32_t MONEY;
int8_t GAME_STATE = 11, MAIN_STATE = 12, BTL_COMMAND_MENU_ACTIVE = 13;
int32_t IS_SCRIPT_PAUSED = 14, CURRENT_MENU = 15;
uint8_t YEAR = 16;
int16_t DAY = 17, HOUR = 18, MINUTE = 19;
uint16_t PLAYTIME_HOURS = 20, PLAYTIME_MINUTES = 21, PLAYTIME_FRAMES = 22;
static PositionData tamer_position, partner_position;
TamerEntity TAMER_ENTITY = {.entity = {.posData = &tamer_position}};
PartnerEntity PARTNER_ENTITY = {.digimonEntity = {.entity = {.posData = &partner_position}}};
PartnerPara PARTNER_PARA;
void plat_input_vblank(void)
{
    if (!port_playtest_started()) return;
    unsigned k = port_playtest_vblank();
    port_pad_latch(k < 2 ? 8 : k < 5 ? 0x4000 : 0);
}
/* DEATH_MODE=segv|assert|watchdog|spin: die at VBlank 7 in a function the symbolizer can name. */
__attribute__((noinline)) void death_segv_here(void) { *(volatile int *)0 = 1; }
__attribute__((noinline)) void death_assert_here(void) { PORT_ASSERT(0 && "synthetic"); }
__attribute__((noinline)) void death_spin_here(void) { for (volatile int i = 0;; i++) {} }
__attribute__((noinline)) void death_watchdog_here(void) { for (;;) clock_psyq_entry(); }
static void die(void)
{
    const char *mode = getenv("DEATH_MODE");
    if (!mode || port_playtest_vblank() != 7) return;
    if (!strcmp(mode, "segv")) death_segv_here();
    if (!strcmp(mode, "assert")) death_assert_here();
    if (!strcmp(mode, "fpe")) raise(SIGFPE);
    if (!strcmp(mode, "spin")) death_spin_here();
    if (!strcmp(mode, "watchdog")) death_watchdog_here();
}
int dw1_game_main(void)
{
    tamer_position.location.vx = 31; tamer_position.location.vy = 32; tamer_position.location.vz = 33;
    partner_position.location.vx = 41; partner_position.location.vy = 42; partner_position.location.vz = 43;
    PARTNER_ENTITY.digimonEntity.stats.current.currentHP = 51;
    PARTNER_ENTITY.digimonEntity.stats.current.currentMP = 52;
    PARTNER_PARA.condition = 0xabc;
    gpu_reset(1); gpu_set_display(0, 0, 2, 2, 0); gpu_set_display_enable(1);
    clock_advance(clock_vblank_cycle(3));
    port_vsync_return();
    for (;;) {
        gpu_fill(0, 0, 2, 2, port_pad_value() & 255);
        (void)rand();
        die();
        CURRENT_SCREEN_ID = (uint16_t)port_playtest_vblank();
        MONEY = -(int32_t)port_playtest_vblank();
        clock_advance(clock_vblank_cycle(1));
    }
}
int main(int argc, char **argv)
{
    if (argc != 6 && argc != 8) return 2;
    const char *death = getenv("DEATH_FILE");
    if (death) port_death_install(open(death, O_WRONLY | O_CREAT | O_TRUNC, 0600));
    PortCapture config = {.state_fd = -1, .input_fd = -1, .vblank_limit = getenv("DEATH_MODE") ? 0 : 10, .digest = strcmp(argv[2], "-") ? argv[2] : NULL,
                          .clock_trace = argv[3], .digest_every = 3};
    if (!strcmp(argv[1], "live")) config.record_inputs = argv[4];
    else config.inputs = argv[4];
    if (!strcmp(argv[5], "off")) config.record_inputs = NULL;
    if (argc == 8) { config.state_fd = atoi(argv[6]); config.input_fd = atoi(argv[7]); }
    return port_run_game(0, 0, &config);
}
