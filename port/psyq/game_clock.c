/* Measured CPU work in fixed-size game initialization loops. No MIPS code
 * is interpreted: ordinary compiled C functions select their measured work.
 * Kept outside scene observation so every build uses the same clock model. */
#include "clock.h"
#include <stdio.h>
#include <stdlib.h>
extern void initializeMusic(void) __attribute__((weak));

extern void initializeModelComponents(void) __attribute__((weak));
extern void initializeWorldObjects(void) __attribute__((weak));
extern void initializeGsTMDMap(void) __attribute__((weak));
extern void initializeFileReadQueue(void) __attribute__((weak));
extern void initializeAttackObjects(void) __attribute__((weak));
extern void initializeFontCLUT(void) __attribute__((weak));
extern void initializeFadeData(void) __attribute__((weak));
extern void view_init(void) __attribute__((weak));
extern void initializeUIBoxData(void) __attribute__((weak));
extern void initializeInventoryModules(void) __attribute__((weak));
extern void initializeClockData(void) __attribute__((weak));
extern void readVHBFile(void) __attribute__((weak));
extern void readFile(void) __attribute__((weak));
extern void finalizeMusic(void) __attribute__((weak));
extern void seqStop(void) __attribute__((weak));
extern void seqClose(void) __attribute__((weak));
extern short SEQ_ACCESS_NUM __attribute__((weak));
static void probe(void *function, const char *event)
{
    if (!getenv("DW1_DEVICE_PROBE")) return;
    const char *name = function == (void *)initializeMusic ? "initializeMusic" :
        function == (void *)readVHBFile ? "readVHBFile" :
        function == (void *)readFile ? "readFile" :
        function == (void *)finalizeMusic ? "finalizeMusic" : NULL;
    if (name) fprintf(stderr,"game-probe %s %s %llu\n",name,event,(unsigned long long)clock_now());
}

void clock_game_function_exit(void *function)
{
    if (function == (void *)readVHBFile) clock_advance_cost(CLOCK_GAME_VHB_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
    if (function == (void *)readFile) clock_advance_cost(CLOCK_GAME_READ_FILE_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
    probe(function,"exit");
}
void clock_game_function_enter(void *function)
{
    probe(function,"enter");
    if (function == (void *)finalizeMusic) { clock_advance_cost(CLOCK_GAME_FINALIZE_MUSIC_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)seqStop) { clock_advance_cost(CLOCK_GAME_SEQ_STOP_CPU_CYCLES(SEQ_ACCESS_NUM != -1), CLOCK_COST_CPU); return; }
    if (function == (void *)seqClose) { clock_advance_cost(CLOCK_GAME_SEQ_CLOSE_CPU_CYCLES(SEQ_ACCESS_NUM != -1), CLOCK_COST_CPU); return; }
    if (function == (void *)readVHBFile) { clock_advance_cost(CLOCK_GAME_VHB_ENTER_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)readFile) { clock_advance_cost(CLOCK_GAME_READ_FILE_ENTER_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeModelComponents) { clock_advance_cost(CLOCK_GAME_MODEL_COMPONENTS_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeWorldObjects) { clock_advance_cost(CLOCK_GAME_WORLD_OBJECTS_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeGsTMDMap) { clock_advance_cost(CLOCK_GAME_GS_TMD_MAP_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeFileReadQueue) { clock_advance_cost(CLOCK_GAME_FILE_QUEUE_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeAttackObjects) { clock_advance_cost(CLOCK_GAME_ATTACK_OBJECTS_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeFontCLUT) { clock_advance_cost(CLOCK_GAME_FONT_CLUT_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeFadeData) { clock_advance_cost(CLOCK_GAME_FADE_DATA_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)view_init) { clock_advance_cost(CLOCK_GAME_VIEW_INIT_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeUIBoxData) { clock_advance_cost(CLOCK_GAME_UI_BOX_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeInventoryModules) { clock_advance_cost(CLOCK_GAME_INVENTORY_CPU_CYCLES, CLOCK_COST_CPU); return; }
    if (function == (void *)initializeClockData) { clock_advance_cost(CLOCK_GAME_CLOCK_CPU_CYCLES, CLOCK_COST_CPU); return; }
}
