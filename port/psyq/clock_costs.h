#ifndef DW1_CLOCK_COSTS_H
#define DW1_CLOCK_COSTS_H
/* [hypothesis] CPU cycles per decoded 16x16 macroblock; uncalibrated.
 * Story 3.10 must calibrate this against movie0. */
#define CLOCK_MDEC_MACROBLOCK_CYCLES 1024u
/* [hypothesis] Pause second-response averages in PS1 CPU cycles, measured
 * on a PAL PSone by psx-spx, CDROM - Response Timings:
 * https://psx-spx.consoledev.net/cdromdrive/#cdrom-response-timings
 * Select by drive state and speed, not by scene. Story 3.10 must recalibrate
 * these averages against movie0/movie0_skip and other CD users. */
#define CLOCK_CD_PAUSE_1X_CYCLES 0x21181cu
#define CLOCK_CD_PAUSE_2X_CYCLES 0x10bd93u
#define CLOCK_CD_PAUSE_IDLE_CYCLES 0x1df2u
/* [hypothesis] memory-card command and 128-byte sector costs in CPU cycles.
 * Story 3.10 must calibrate against OpenBIOS/Redux; never tune per scene. */
#define CLOCK_CARD_COMMAND_CYCLES 1024u
#define CLOCK_CARD_SECTOR_CYCLES 33869u
#endif
