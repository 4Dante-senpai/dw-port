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
/* Redux NTSC 320x240: 144485888 cycles / 256 VBlanks = 564398 cycles/VBlank.
 * Disc-free GP1(08h) probe: tools/port/reference/measure_clock.lua, story 3.10.
 * 33868800 / 564398 = 60.0087172527 Hz. This is the reference emulator's rate,
 * rather than a claim about physical PS1 hardware.
 * Evidence and pinned emulator SHA-256: docs/port/clock-calibration.md. */
#define CLOCK_VBLANK_NUM 282199u
#define CLOCK_VBLANK_DEN 16934400u
#include "clock_costs.h"

typedef void (*ClockFn)(void *arg);

/* Exclusive elapsed virtual cycles, not sums of overlapping device reservations. */
typedef enum {
	CLOCK_COST_CD, CLOCK_COST_GPU, CLOCK_COST_MDEC, CLOCK_COST_CARD,
	CLOCK_COST_PSYQ, CLOCK_COST_WAIT, CLOCK_COST_CPU, CLOCK_COST_SPU, CLOCK_COST_COUNT
} ClockCost;
extern const char *const clock_cost_names[CLOCK_COST_COUNT];
void clock_cost_snapshot(uint64_t totals[CLOCK_COST_COUNT]);
/* CPU work resumes after interrupt service; device/wait reservations remain
 * elapsed deadlines. Generic uncalibrated PsyQ entries retain elapsed costs. */
void clock_advance_cost(uint64_t cycles, ClockCost source);
void clock_wait_cost(int (*done)(void *), void *arg, ClockCost source);

/* Cycle at which VBlank number k (1-based) happens, rounded down, with no accumulated error. */
uint64_t clock_vblank_cycle(uint32_t k);
/* Whole video lines in a span of cycles (same ratio as the VBlank). */
uint64_t clock_lines(uint64_t cycles);

uint64_t clock_now(void);
/* Measured CPU polling branch; device IRQs are tested at its end. */
void clock_poll_branch(unsigned cycles);
uint64_t clock_poll_boundary(void);
uint32_t clock_vblanks(void); /* VBlanks dispatched so far */
int clock_in_handler(void);   /* an event handler is running */

/* Advances the clock and runs, in order, every event due on the way. */
void clock_advance(uint64_t cycles);
void clock_game_function_enter(void *function);
void clock_game_function_exit(void *function);
void clock_psyq_entry(void); /* clock_advance(CLOCK_PSYQ_ENTRY_CYCLES) */

/* Runs events (jumping the clock to each one) until done(arg) is true. Not from a handler. */
void clock_wait_until(int (*done)(void *), void *arg);

/* One-shot event delay cycles from now; returns an id for clock_cancel (ids are never reused
 * while their event is pending, and cancelling a fired or cancelled id does nothing). */
int clock_schedule(uint64_t delay, ClockFn fn, void *arg);
void clock_cancel(int id);

/* Called on every VBlank, after the VBlank count is incremented (libetc's trap). */
void clock_set_vblank_handler(void (*fn)(void));

/* Host observation after the VBlank callback and events due at that cycle. */
void clock_set_vblank_observer(void (*fn)(void));

/* Optional trace of dispatched events (cycle, kind) for determinism tests; kind -1 = VBlank. */
void clock_set_trace(void (*fn)(uint64_t cycle, int kind, void *arg));

/* Back to cycle 0 with an empty queue and no owner thread (boot and tests). */
void clock_reset(void);

#endif
