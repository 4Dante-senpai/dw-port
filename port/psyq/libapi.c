/* libapi: root counters over the virtual clock (AD-8). asm:asm/us/main/psyq/libapi.s (mw_lib41) */
#include <stdint.h>

#include "clock.h"
#include "port.h"
#include "psyq.h"

#define IRQ_RCNT0 4

typedef struct {
	uint16_t mode;   /* hardware mode bits, as SetRCnt computes them */
	uint16_t target;
	uint64_t start;  /* cycle at which the count was 0 */
	int event;       /* pending target event, -1 if none */
} RootCounter;

static RootCounter counters[3] = { { 0, 0, 0, -1 }, { 0, 0, 0, -1 }, { 0, 0, 0, -1 } };

static int hblank_mode(int rc)
{
	return rc == 1 && (counters[rc].mode & 0x100);
}

/* Cycles per tick: sys/8 for counter 2 (mode bit 0x200), else the system clock. Counter 1 in
 * HBlank mode counts video lines (clock_lines) instead. */
static uint64_t tick_cycles(int rc)
{
	return (rc == 2 && (counters[rc].mode & 0x200)) ? 8 : 1;
}

static long counter_value(int rc)
{
	uint64_t dt = clock_now() - counters[rc].start;
	return (long)((hblank_mode(rc) ? clock_lines(dt) : dt / tick_cycles(rc)) & 0xFFFF);
}

/* [hypothesis] with reset-at-target the period is `target` ticks, and target 0 counts the full
 * 0x10000; checked against the reference audio trace in story 3.5 (SS_TICK240). */
static uint64_t period(int rc)
{
	uint64_t t = counters[rc].target ? counters[rc].target : 0x10000;
	return t * tick_cycles(rc);
}

static void on_target(void *arg)
{
	int rc = (int)(intptr_t)arg;
	counters[rc].start = clock_now();
	counters[rc].event = clock_schedule(period(rc), on_target, arg);
	if (counters[rc].mode & 0x10)
		psyq_raise_irq(IRQ_RCNT0 + rc);
}

/* Target events are needed only for the IRQ; without it the value is computed on demand. */
static void arm(int rc)
{
	clock_cancel(counters[rc].event);
	counters[rc].event = -1;
	counters[rc].start = clock_now();
	if (counters[rc].mode & 0x10) {
		/* Unsupported by the model, and not used by DW1's libraries: fail loudly. */
		PORT_ASSERT(!hblank_mode(rc));
		counters[rc].event = clock_schedule(period(rc), on_target, (void *)(intptr_t)rc);
	}
}

/* asm: SetRCnt -- programs target and mode (reset at target, IRQ on target for RCntMdINTR,
 * sys/8 for counter 2 and HBlank for counter 1 unless RCntMdSC). Returns 1, or 0 if spec > 2. */
long SetRCnt(unsigned long spec, unsigned short target, long mode)
{
	clock_psyq_entry();
	int rc = spec & 0xFFFF;
	if (rc >= 3)
		return 0;
	uint16_t hw = 0x48;
	if (rc < 2) {
		if (mode & 0x10)
			hw = 0x49;
		if (!(mode & RCntMdSC))
			hw |= 0x100;
	} else if (!(mode & RCntMdSC)) {
		hw = 0x248;
	}
	if (mode & RCntMdINTR)
		hw |= 0x10;
	/* Not modeled (and unused by DW1): gate/sync mode, and the dot clock of counter 0. */
	PORT_ASSERT(!(hw & 0x1));
	PORT_ASSERT(!(rc == 0 && (hw & 0x100)));
	counters[rc].mode = hw;
	counters[rc].target = target;
	arm(rc);
	return 1;
}

/* asm: GetRCnt -- current counter value, or 0 if spec > 2. */
long GetRCnt(unsigned long spec)
{
	clock_psyq_entry();
	int rc = spec & 0xFFFF;
	if (rc >= 3)
		return 0;
	return counter_value(rc);
}

/* asm: ResetRCnt -- counter value back to 0. Returns 1, or 0 if spec > 2. */
long ResetRCnt(unsigned long spec)
{
	clock_psyq_entry();
	int rc = spec & 0xFFFF;
	if (rc >= 3)
		return 0;
	arm(rc);
	return 1;
}

void psyq_libapi_reset(void)
{
	for (int i = 0; i < 3; i++) {
		clock_cancel(counters[i].event);
		counters[i] = (RootCounter){ 0, 0, 0, -1 };
	}
}
