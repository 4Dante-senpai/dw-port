#include "clock.h"

#include <pthread.h>

#include "port.h"

#define MAX_EVENTS 32
#define KIND_VBLANK (-1)

typedef struct {
	uint64_t due;
	uint64_t seq; /* tie-break: events due at the same cycle run in scheduling order */
	ClockFn fn;
	void *arg;
	int id;       /* slot + MAX_EVENTS * generation; 0 when free */
} Event;

static uint64_t now;
static uint64_t next_seq;
static uint32_t vblanks;
static Event events[MAX_EVENTS];
static int generation = 1;
static int in_handler;
static void (*vblank_handler)(void);
static void (*vblank_observer)(void);
/* Keep observation pending if a callback advances time past run_until's target. */
static int observed_vblank;
static void (*trace)(uint64_t, int, void *);
static pthread_t owner;
static int has_owner;

/* Losing an event or racing on the clock corrupts the game: fatal in every build. */
#define CLOCK_FATAL(cond, msg)                                                                 \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			fprintf(stderr, "clock: %s\n", msg);                                           \
			abort();                                                                       \
		}                                                                                  \
	} while (0)

static void check_thread(void)
{
	if (!has_owner) {
		owner = pthread_self();
		has_owner = 1;
	}
	CLOCK_FATAL(pthread_equal(owner, pthread_self()), "used from a second thread");
}

uint64_t clock_vblank_cycle(uint32_t k)
{
	/* Split k so the product cannot overflow 64 bits. */
	const uint64_t per = (uint64_t)CLOCK_HZ * CLOCK_VBLANK_NUM;
	return (k / CLOCK_VBLANK_DEN) * per + (k % CLOCK_VBLANK_DEN) * per / CLOCK_VBLANK_DEN;
}

uint64_t clock_lines(uint64_t cycles)
{
	const uint64_t per = (uint64_t)CLOCK_HZ * CLOCK_VBLANK_NUM; /* cycles per frame * DEN */
	uint64_t frames = cycles * CLOCK_VBLANK_DEN / per;           /* whole frames, no overflow for */
	uint64_t rest = cycles - clock_vblank_cycle((uint32_t)frames); /* < 2^32 frames */
	return frames * CLOCK_LINES_PER_FRAME + rest * CLOCK_LINES_PER_FRAME * CLOCK_VBLANK_DEN / per;
}

uint64_t clock_now(void) { return now; }
uint32_t clock_vblanks(void) { return vblanks; }
int clock_in_handler(void) { return in_handler; }

void clock_set_vblank_handler(void (*fn)(void))
{
	check_thread();
	vblank_handler = fn;
}

void clock_set_vblank_observer(void (*fn)(void))
{
	check_thread();
	vblank_observer = fn;
}

void clock_set_trace(void (*fn)(uint64_t, int, void *))
{
	check_thread();
	trace = fn;
}

void clock_reset(void)
{
	now = next_seq = 0;
	vblanks = 0;
	for (int i = 0; i < MAX_EVENTS; i++)
		events[i].id = 0;
	in_handler = 0;
	observed_vblank = 0;
	has_owner = 0;
}

int clock_schedule(uint64_t delay, ClockFn fn, void *arg)
{
	check_thread();
	for (int i = 0; i < MAX_EVENTS; i++) {
		if (!events[i].id) {
			int id = i + MAX_EVENTS * generation;
			generation = generation % 0x1000000 + 1;
			events[i] = (Event){ now + delay, next_seq++, fn, arg, id };
			return id;
		}
	}
	CLOCK_FATAL(0, "event queue full");
	return -1;
}

void clock_cancel(int id)
{
	check_thread();
	if (id > 0 && events[id % MAX_EVENTS].id == id)
		events[id % MAX_EVENTS].id = 0;
}

/* Due cycle of the next event of any kind (a VBlank is always pending); *index -1 = VBlank. */
static uint64_t next_due(int *index)
{
	uint64_t vb = clock_vblank_cycle(vblanks + 1);
	int best = -1;
	for (int i = 0; i < MAX_EVENTS; i++) {
		if (events[i].id && (best < 0 || events[i].due < events[best].due ||
		                     (events[i].due == events[best].due && events[i].seq < events[best].seq)))
			best = i;
	}
	/* A device event due at the VBlank cycle runs after the VBlank. */
	*index = (best >= 0 && events[best].due < vb) ? best : -1;
	return *index >= 0 ? events[*index].due : vb;
}

static void dispatch(int index)
{
	in_handler = 1;
	if (index < 0) {
		vblanks++;
		if (trace)
			trace(now, KIND_VBLANK, NULL);
		if (vblank_handler)
			vblank_handler();
	} else {
		Event e = events[index];
		events[index].id = 0;
		if (trace)
			trace(now, index, e.arg);
		e.fn(e.arg);
	}
	in_handler = 0;
}

/* Runs every event due at or before target, in order; time never goes backwards. */
static void run_until(uint64_t target)
{
	for (;;) {
		int index;
		uint64_t due = next_due(&index);
		if (observed_vblank && due > now) {
			observed_vblank = 0;
			if (vblank_observer)
				vblank_observer();
		}
		if (due > target)
			break;
		if (due > now)
			now = due;
		dispatch(index);
		if (index < 0)
			observed_vblank = 1;
	}
	if (target > now)
		now = target;
}

void clock_advance(uint64_t cycles)
{
	check_thread();
	if (in_handler) {
		now += cycles; /* masked: the outer loop dispatches what became due */
		return;
	}
	run_until(now + cycles);
}

void clock_psyq_entry(void)
{
	clock_advance(CLOCK_PSYQ_ENTRY_CYCLES);
}

void clock_wait_until(int (*done)(void *), void *arg)
{
	check_thread();
	CLOCK_FATAL(!in_handler, "blocking wait inside an event handler (interrupts are masked)");
	while (!done(arg)) {
		int index;
		uint64_t due = next_due(&index);
		run_until(due);
	}
}
