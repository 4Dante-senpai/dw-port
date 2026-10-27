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
static uint64_t poll_boundary;
static uint64_t next_seq;
static uint32_t vblanks;
static Event events[MAX_EVENTS];
/* Polling usually leaves the queue unchanged. Cache its minimum so each
 * compiled VSync/CD polling branch need not scan all event slots. -2 is dirty,
 * -1 is an empty queue; VBlank is still compared independently on each query. */
static int next_event = -2;
static int generation = 1;
static int in_handler;
static void (*vblank_handler)(void);
static void (*vblank_observer)(void);
/* Keep observation pending if a callback advances time past run_until's target. */
static int observed_vblank;
static void (*trace)(uint64_t, int, void *);
static pthread_t owner;
static int has_owner;
static uint64_t costs[CLOCK_COST_COUNT];
static ClockCost cost_source = CLOCK_COST_CPU;
const char *const clock_cost_names[CLOCK_COST_COUNT] = {
	"cd", "gpu", "mdec", "card", "psyq", "wait", "cpu", "spu"
};
void clock_cost_snapshot(uint64_t totals[CLOCK_COST_COUNT])
{
	for (unsigned i = 0; i < CLOCK_COST_COUNT; i++) totals[i] = costs[i];
}
static void advance_to(uint64_t cycle)
{
	if (cycle > now) { costs[cost_source] += cycle - now; now = cycle; }
}

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
	/* Split the period, not k: a large rational denominator otherwise overflows
	 * k * per after only hours of gameplay. Both products fit for all uint32_t k. */
	const uint64_t per = (uint64_t)CLOCK_HZ * CLOCK_VBLANK_NUM;
	return (uint64_t)k * (per / CLOCK_VBLANK_DEN) +
	       (uint64_t)k * (per % CLOCK_VBLANK_DEN) / CLOCK_VBLANK_DEN;
}

uint64_t clock_lines(uint64_t cycles)
{
	/* The measured Redux period is an integer CPU-cycle count. Split the span
	 * before multiplying, so long sessions do not overflow cycles * DEN. */
	const uint64_t period = clock_vblank_cycle(1);
	return cycles / period * CLOCK_LINES_PER_FRAME +
	       cycles % period * CLOCK_LINES_PER_FRAME / period;
}

uint64_t clock_now(void) { return now; }
uint64_t clock_poll_boundary(void) { return poll_boundary; }
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
	now = next_seq = poll_boundary = 0;
	vblanks = 0;
	next_event = -2;
	for (int i = 0; i < MAX_EVENTS; i++)
		events[i].id = 0;
	in_handler = 0;
	observed_vblank = 0;
	has_owner = 0;
	for (unsigned i = 0; i < CLOCK_COST_COUNT; i++) costs[i] = 0;
	cost_source = CLOCK_COST_CPU;
}

int clock_schedule(uint64_t delay, ClockFn fn, void *arg)
{
	check_thread();
	for (int i = 0; i < MAX_EVENTS; i++) {
		if (!events[i].id) {
			int id = i + MAX_EVENTS * generation;
			generation = generation % 0x1000000 + 1;
			events[i] = (Event){ now + delay, next_seq++, fn, arg, id };
			next_event = -2;
			return id;
		}
	}
	CLOCK_FATAL(0, "event queue full");
	return -1;
}

void clock_cancel(int id)
{
	check_thread();
	if (id > 0 && events[id % MAX_EVENTS].id == id) {
		events[id % MAX_EVENTS].id = 0;
		next_event = -2;
	}
}

/* Due cycle of the next event of any kind (a VBlank is always pending); *index -1 = VBlank. */
static uint64_t next_due(int *index)
{
	uint64_t vb = clock_vblank_cycle(vblanks + 1);
	if (next_event == -2) {
		next_event = -1;
		for (int i = 0; i < MAX_EVENTS; i++) {
			if (events[i].id && (next_event < 0 || events[i].due < events[next_event].due ||
			                     (events[i].due == events[next_event].due && events[i].seq < events[next_event].seq)))
				next_event = i;
		}
	}
	int best = next_event;
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
		next_event = -2;
		if (trace)
			trace(now, index, e.arg);
		e.fn(e.arg);
	}
	in_handler = 0;
}

/* Runs every event due at or before target, in order; time never goes backwards. */
static void run_until(uint64_t target, int cpu_work)
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
		advance_to(due);
		uint64_t interrupted = now;
		dispatch(index);
		/* Interrupt service consumes CPU while a device deadline keeps running.
		 * Preserve the interrupted instruction work, including the remainder
		 * after a short DMA completes inside a public API return path. */
		if (cpu_work) target += now - interrupted;
		if (index < 0)
			observed_vblank = 1;
	}
	advance_to(target);
}

void clock_advance(uint64_t cycles)
{
	check_thread();
	if (in_handler) {
		advance_to(now + cycles); /* masked: the outer loop dispatches what became due */
		return;
	}
	run_until(now + cycles, 0);
}

void clock_psyq_entry(void)
{
	clock_advance_cost(CLOCK_PSYQ_ENTRY_CYCLES, CLOCK_COST_PSYQ);
}

void clock_advance_cost(uint64_t cycles, ClockCost source)
{
	ClockCost previous = cost_source;
	cost_source = source;
	if (!in_handler && source == CLOCK_COST_CPU) {
		check_thread();
		run_until(now + cycles, 1);
	} else clock_advance(cycles);
	cost_source = previous;
}

/* A compiled polling loop checks devices only after a taken branch. Keep
 * device deadlines running through the straight-line CPU work and dispatch
 * them at its measured branch boundary (Redux interpreter branchTest). */
void clock_poll_branch(unsigned cycles)
{
	check_thread();
	CLOCK_FATAL(!in_handler, "polling branch inside interrupt handler");
	ClockCost previous = cost_source;
	cost_source = CLOCK_COST_CPU;
	advance_to(now + cycles);
	poll_boundary = now;
	int pending;
	do { run_until(now, 0); } while (next_due(&pending) <= now);
	poll_boundary = 0;
	cost_source = previous;
}

void clock_wait_cost(int (*done)(void *), void *arg, ClockCost source)
{
	ClockCost previous = cost_source;
	cost_source = source;
	clock_wait_until(done, arg);
	cost_source = previous;
}

void clock_wait_until(int (*done)(void *), void *arg)
{
	check_thread();
	CLOCK_FATAL(!in_handler, "blocking wait inside an event handler (interrupts are masked)");
	while (!done(arg)) {
		int index;
		uint64_t due = next_due(&index);
		run_until(due, 0);
	}
}

__attribute__((weak)) void clock_game_function_exit(void *function) { (void)function; }
