/* libapi: root counters and the game heap. asm:asm/us/main/psyq/libapi.s (mw_lib41) */
#include <stdint.h>

#include "clock.h"
#include "port.h"
#include "ps1mem.h"
#include "psyq.h"

#define IRQ_RCNT0 4

typedef struct {
	uint16_t mode;   /* hardware mode bits, as SetRCnt computes them */
	uint16_t target;
	uint64_t start;  /* cycle at which the count was 0 */
	int event;       /* pending target event, -1 if none */
} RootCounter;

static RootCounter counters[3] = { { 0, 0, 0, -1 }, { 0, 0, 0, -1 }, { 0, 0, 0, -1 } };

typedef struct HeapBlock {
	struct HeapBlock *next;
	uint32_t units;
} HeapBlock;

_Static_assert(sizeof(HeapBlock) == 8, "PsyQ heap headers occupy one 8-byte unit");
static HeapBlock *freep;

/* asm:asm/us/main/psyq/libapi.s:InitHeap3 -- round size down to 8 bytes and put a
 * zero-sized sentinel at the end of the circular free list. addr is a PS1 address. */
void InitHeap3(unsigned long *addr, unsigned long size)
{
	clock_psyq_entry();
	if (!addr || !size)
		return;
	uint32_t ps1_addr = (uint32_t)(uintptr_t)addr;
	PORT_ASSERT(ps1_addr_is_ram(ps1_addr));
	size &= ~7u;
	PORT_ASSERT(size >= 8 && size <= PS1_RAM_SIZE - (ps1_addr & (PS1_RAM_SIZE - 1)));
	PORT_ASSERT((ps1_addr & 3) == 0);
	HeapBlock *first = ps1_ram(ps1_addr);
	HeapBlock *sentinel = (HeapBlock *)((uint8_t *)first + size - 8);
	sentinel->next = first;
	sentinel->units = 0;
	first->next = sentinel;
	first->units = ((size + 7) >> 3) - 1;
	freep = first;
}

/* asm:asm/us/main/psyq/libapi.s:malloc3 -- round up to 8 bytes, add the header,
 * then split from the end of the first fitting free block (or unlink an exact fit). */
void *malloc3(size_t size)
{
	clock_psyq_entry();
	if (!size)
		return NULL;
	uint32_t rounded = ((uint32_t)size + 7u) & ~7u;
	uint32_t units = ((rounded + 7u) >> 3) + 1;
	PORT_ASSERT(freep != NULL);
	HeapBlock *prev = freep;
	for (HeapBlock *p = prev->next;; prev = p, p = p->next) {
		if ((int32_t)p->units >= (int32_t)units) {
			if (p->units == units) {
				prev->next = p->next;
			} else {
				p->units -= units;
				p = (HeapBlock *)((uint8_t *)p + p->units * 8);
				p->units = units;
			}
			freep = prev;
			return (uint8_t *)p + 8;
		}
		if (p == freep)
			return NULL;
	}
}

/* asm:asm/us/main/psyq/libapi.s:free3 -- insert by address and coalesce both neighbors,
 * except the zero-sized sentinel. Unread _r_bp/_r_p/_r_freep debug writes are omitted. */
void free3(void *ptr)
{
	clock_psyq_entry();
	HeapBlock *block = (HeapBlock *)((uint8_t *)ptr - 8);
	HeapBlock *p = freep;
	while (!((uintptr_t)p < (uintptr_t)block && (uintptr_t)block < (uintptr_t)p->next)) {
		if ((uintptr_t)p >= (uintptr_t)p->next &&
		    ((uintptr_t)p < (uintptr_t)block || (uintptr_t)block < (uintptr_t)p->next))
			break;
		p = p->next;
	}
	if ((uint8_t *)block + block->units * 8 == (uint8_t *)p->next && p->next->units) {
		block->units += p->next->units;
		block->next = p->next->next;
	} else {
		block->next = p->next;
	}
	if ((uint8_t *)p + p->units * 8 == (uint8_t *)block) {
		p->units += block->units;
		p->next = block->next;
	} else {
		p->next = block;
	}
	freep = p;
}

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
	/* The counter resets at the target, independently of CPU interrupt latency.
	 * A masked handler can postpone dispatch past one or more targets: retain
	 * the /8 phase and coalesce those targets into the pending interrupt. */
	uint64_t span = period(rc);
	uint64_t elapsed = clock_now() - counters[rc].start;
	counters[rc].start += elapsed / span * span;
	counters[rc].event = clock_schedule(span - elapsed % span, on_target, arg);
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
	int rc = spec & 0xFFFF;
	if (rc == 2) clock_advance_cost(CLOCK_RCNT2_MODE_WRITE_CPU_CYCLES, CLOCK_COST_CPU);
	else clock_psyq_entry();
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
	if (rc == 2) clock_advance_cost(CLOCK_RCNT2_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
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
	freep = NULL;
	for (int i = 0; i < 3; i++) {
		clock_cancel(counters[i].event);
		counters[i] = (RootCounter){ 0, 0, 0, -1 };
	}
}
