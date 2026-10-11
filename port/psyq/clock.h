#ifndef PORT_CLOCK_H
#define PORT_CLOCK_H

/*
 * Virtual time of the port (AD-8). The only owner of time: a count of PS1 CPU cycles and a queue
 * of events (VBlank, root counters, end of device operations). Every PsyQ entry advances it, and a
 * blocking wait jumps straight to the next event. Events run on the thread that drives the clock,
 * one at a time: while an event handler runs, interrupts are masked as on the console, so time
 * advanced by PsyQ calls inside the handler is accounted for but its events wait until it returns.
 */

#include <stdint.h>

#define CLOCK_HZ 33868800u
/*
 * [hypothesis] VBlank period in CPU cycles = CLOCK_HZ * CLOCK_VBLANK_NUM / CLOCK_VBLANK_DEN, i.e.
 * the 59.94 Hz (60000/1001) of NFR-1. Per psx-spx, NTSC non-interlaced video (263 lines) runs at
 * ~59.83 Hz; the exact ratio is measured against the reference emulator in story 3.10.
 */
#define CLOCK_VBLANK_NUM 1001u
#define CLOCK_VBLANK_DEN 60000u
/* [hypothesis] fixed cost of one PsyQ call; calibrated against the reference in story 3.10 */
#define CLOCK_PSYQ_ENTRY_CYCLES 64u
/* [hypothesis] NTSC lines per frame, for VSync(1) and root counter 1; checked in story 3.10 */
#define CLOCK_LINES_PER_FRAME 263u

typedef void (*ClockFn)(void *arg);

/* Cycle at which VBlank number k (1-based) happens, rounded down, with no accumulated error. */
uint64_t clock_vblank_cycle(uint32_t k);
/* Whole video lines in a span of cycles (same ratio as the VBlank). */
uint64_t clock_lines(uint64_t cycles);

uint64_t clock_now(void);
uint32_t clock_vblanks(void); /* VBlanks dispatched so far */
int clock_in_handler(void);   /* an event handler is running */

/* Advances the clock and runs, in order, every event due on the way. */
void clock_advance(uint64_t cycles);
void clock_psyq_entry(void); /* clock_advance(CLOCK_PSYQ_ENTRY_CYCLES) */

/* Runs events (jumping the clock to each one) until done(arg) is true. Not from a handler. */
void clock_wait_until(int (*done)(void *), void *arg);

/* One-shot event delay cycles from now; returns an id for clock_cancel (ids are never reused
 * while their event is pending, and cancelling a fired or cancelled id does nothing). */
int clock_schedule(uint64_t delay, ClockFn fn, void *arg);
void clock_cancel(int id);

/* Called on every VBlank, after the VBlank count is incremented (libetc's trap). */
void clock_set_vblank_handler(void (*fn)(void));

/* Optional trace of dispatched events (cycle, kind) for determinism tests; kind -1 = VBlank. */
void clock_set_trace(void (*fn)(uint64_t cycle, int kind, void *arg));

/* Back to cycle 0 with an empty queue and no owner thread (boot and tests). */
void clock_reset(void);

#endif
