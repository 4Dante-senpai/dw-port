/* L1: virtual clock, VSync semantics and root counters (AD-8). */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../clock.h"
#include "../psyq.h"

static int failures;
#define CHECK(cond)                                                                        \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
			failures++;                                                                    \
		}                                                                                  \
	} while (0)

static void reset(void)
{
	psyq_libapi_reset();
	clock_reset();
	psyq_libetc_reset();
	ResetCallback();
}

/* Runs fn in a child process; returns whether it aborted. */
static int aborts(void (*fn)(void))
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		freopen("/dev/null", "w", stderr);
		setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }); /* expected abort: no core dump */
		fn();
		_exit(0);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void no_drift(void)
{
	reset();
	/* 10 minutes = 35964 VBlanks (+ a fraction) at 60000/1001 Hz. */
	uint64_t ten_min = 600ull * CLOCK_HZ;
	clock_advance(ten_min);
	CHECK(clock_vblanks() == 35964);
	CHECK(clock_vblank_cycle(35964) == 35964ull * 33868800u * 1001u / 60000u);
	CHECK(clock_vblank_cycle(35964) <= ten_min && clock_vblank_cycle(35965) > ten_min);
	CHECK(Vcount == 35964);
}

static void vsync_semantics(void)
{
	reset();
	clock_advance(clock_vblank_cycle(1) / 2); /* mid frame 1 */
	CHECK(VSync(-1) == 0);
	VSync(0);
	CHECK(Vcount == 1 && clock_now() == clock_vblank_cycle(1));
	VSync(0);
	CHECK(Vcount == 2);
	/* VSync(3): 3 VBlanks after the last return (VBlank 2), called right away. */
	VSync(3);
	CHECK(Vcount == 5);
	/* Called late (after VBlank 7): VSync(2) waits only for the next VBlank. */
	clock_advance(clock_vblank_cycle(7) - clock_now() + 10);
	VSync(2);
	CHECK(Vcount == 8);
	/* VSync(1) counts lines since the last return and does not wait. */
	uint32_t v = clock_vblanks();
	clock_advance(clock_vblank_cycle(1) / 2);
	int lines = VSync(1);
	CHECK(lines >= 130 && lines <= 132);
	CHECK(clock_vblanks() == v);
	CHECK(VSync(-1) == Vcount);
}

static volatile int flag;
static void set_flag(void) { flag = 1; }

static void polling_terminates(void)
{
	reset();
	flag = 0;
	VSyncCallback(set_flag);
	int polls = 0;
	while (!flag && polls < 100000) {
		VSync(-1);
		polls++;
	}
	CHECK(flag && polls < 100000);
	CHECK(Vcount == 1);
	CHECK(VSyncCallback(NULL) == (int)(uintptr_t)set_flag); /* returns the previous callback */
}

static int rcnt_hits;
static uint64_t rcnt_last;
static void on_rcnt2(void)
{
	rcnt_hits++;
	rcnt_last = clock_now();
}

static void root_counter(void)
{
	reset();
	rcnt_hits = 0;
	InterruptCallback(6, on_rcnt2);
	CHECK(SetRCnt(RCntCNT2, 1000, RCntMdINTR) == 1);
	uint64_t t0 = clock_now(); /* counting starts when SetRCnt returns */
	clock_advance(8 * 1000 * 10);
	CHECK(rcnt_hits == 10);
	CHECK(rcnt_last == t0 + 8 * 1000 * 10);
	/* Each PsyQ call costs CLOCK_PSYQ_ENTRY_CYCLES before it reads the counter. */
	CHECK(GetRCnt(RCntCNT2) == CLOCK_PSYQ_ENTRY_CYCLES / 8);
	clock_advance(8 * 250);
	CHECK(GetRCnt(RCntCNT2) == 2 * CLOCK_PSYQ_ENTRY_CYCLES / 8 + 250);
	CHECK(SetRCnt(0xF2000003u, 10, 0) == 0);
	/* Polling a counter value terminates too. */
	ResetRCnt(RCntCNT2);
	while (GetRCnt(RCntCNT2) < 500) {
	}
	CHECK(GetRCnt(RCntCNT2) >= 500);
}

/* Trace of dispatched events, hashed (FNV-1a). */
static uint64_t trace_hash;
static void trace(uint64_t cycle, int kind, void *arg)
{
	(void)arg;
	uint64_t v[2] = { cycle, (uint64_t)(int64_t)kind };
	const unsigned char *b = (const unsigned char *)v;
	for (size_t i = 0; i < sizeof v; i++)
		trace_hash = (trace_hash ^ b[i]) * 1099511628211ull;
}

/* Full trace of a run, compared byte for byte. */
#define TRACE_MAX 4096
static uint64_t trace_buf[TRACE_MAX][2];
static int trace_len;
static void record(uint64_t cycle, int kind, void *arg)
{
	trace(cycle, kind, arg);
	if (trace_len < TRACE_MAX) {
		trace_buf[trace_len][0] = cycle;
		trace_buf[trace_len][1] = (uint64_t)(int64_t)kind;
		trace_len++;
	}
}

static uint64_t scenario(void)
{
	reset();
	trace_hash = 1469598103934665603ull;
	trace_len = 0;
	clock_set_trace(record);
	InterruptCallback(6, on_rcnt2);
	SetRCnt(RCntCNT2, 4410, RCntMdINTR);
	VSyncCallback(set_flag);
	for (int i = 0; i < 120; i++)
		VSync(i % 3);
	clock_set_trace(NULL);
	return trace_hash;
}

static void deterministic(void)
{
	static uint64_t first[TRACE_MAX][2];
	uint64_t a = scenario();
	int len = trace_len;
	memcpy(first, trace_buf, sizeof first);
	uint64_t b = scenario();
	CHECK(a == b && len == trace_len && len > 100);
	CHECK(memcmp(first, trace_buf, sizeof first) == 0);
	/* Same trace from a fresh process. */
	int fds[2];
	CHECK(pipe(fds) == 0);
	pid_t pid = fork();
	if (pid == 0) {
		scenario();
		write(fds[1], trace_buf, sizeof trace_buf);
		_exit(0);
	}
	static uint64_t child[TRACE_MAX][2];
	size_t got = 0;
	while (got < sizeof child) {
		ssize_t n = read(fds[0], (char *)child + got, sizeof child - got);
		if (n <= 0)
			break;
		got += (size_t)n;
	}
	waitpid(pid, NULL, 0);
	CHECK(got == sizeof child && memcmp(first, child, sizeof first) == 0);
}

static int order[8], order_len;
static void mark(void *arg) { order[order_len++] = (int)(intptr_t)arg; }
static void mark_vblank(void) { order[order_len++] = 100; }

static void same_cycle_order(void)
{
	reset();
	order_len = 0;
	clock_set_vblank_handler(mark_vblank);
	uint64_t vb = clock_vblank_cycle(1);
	clock_schedule(vb - clock_now(), mark, (void *)1); /* due exactly at the VBlank */
	clock_schedule(vb - clock_now(), mark, (void *)2);
	clock_schedule(10, mark, (void *)3);
	clock_schedule(10, mark, (void *)4);
	clock_advance(vb);
	/* same cycle: scheduling order; at the VBlank cycle: the VBlank first */
	CHECK(order_len == 5 && order[0] == 3 && order[1] == 4 && order[2] == 100 && order[3] == 1 &&
	      order[4] == 2);
}

static void cancel_ids(void)
{
	reset();
	order_len = 0;
	int a = clock_schedule(5, mark, (void *)1);
	clock_advance(10); /* a fires, its slot is free */
	int b = clock_schedule(5, mark, (void *)2);
	clock_cancel(a); /* stale id: must not cancel b */
	clock_advance(10);
	CHECK(order_len == 2 && order[1] == 2 && a != b);
	int c = clock_schedule(5, mark, (void *)3);
	clock_cancel(c);
	clock_advance(10);
	CHECK(order_len == 2);
}

static uint64_t inside, after;
static void calls_psyq(void)
{
	GetRCnt(RCntCNT2); /* a PsyQ call inside a handler advances time */
	inside = clock_now();
}

static void delay_counter(void *arg)
{
	clock_advance((uintptr_t)arg); /* interrupts stay masked during this event */
}

static void late_root_counter(void)
{
	reset();
	rcnt_hits = 0;
	InterruptCallback(6, on_rcnt2);
	SetRCnt(RCntCNT2, 1000, RCntMdINTR);
	uint64_t origin = clock_now();
	/* Cross three targets while masked, including a partial /8 tick. One
	 * pending IRQ is delivered; the next target retains its original phase. */
	clock_schedule(7990, delay_counter, (void *)(uintptr_t)17013);
	clock_advance(7990);
	clock_advance(0);
	CHECK(rcnt_hits == 1 && rcnt_last == origin + 25003);
	CHECK(GetRCnt(RCntCNT2) == (1003 + CLOCK_PSYQ_ENTRY_CYCLES) / 8);
	clock_advance(origin + 32000 - clock_now() - 1);
	CHECK(rcnt_hits == 1);
	clock_advance(1);
	CHECK(rcnt_hits == 2 && rcnt_last == origin + 32000);
}

static void handler_calls_psyq(void)
{
	reset();
	InterruptCallback(6, calls_psyq);
	SetRCnt(RCntCNT2, 1, RCntMdINTR); /* 8-cycle period, shorter than one PsyQ call */
	uint64_t t0 = clock_now();
	clock_advance(1000);
	after = clock_now();
	CHECK(after >= inside && inside > t0); /* never backwards, no runaway recursion */
	InterruptCallback(6, NULL);
	SetRCnt(RCntCNT2, 1000, 0); /* no IRQ: no events */
	uint64_t t1 = clock_now();
	clock_advance(100000);
	CHECK(clock_now() == t1 + 100000);
	CHECK(GetRCnt(RCntCNT2) == (long)(((100000 + CLOCK_PSYQ_ENTRY_CYCLES) / 8) & 0xFFFF));
}

static void vsync_from_trap(void) { VSync(0); }

static void wait_in_handler(void)
{
	reset();
	VSyncCallback(vsync_from_trap); /* a blocking wait from the VBlank trap */
	clock_advance(clock_vblank_cycle(2));
}

static void handler_wait_aborts(void)
{
	CHECK(aborts(wait_in_handler));
}

static int slot2_calls;
static void slot2(void) { slot2_calls++; }

static void reset_callback_once(void)
{
	psyq_libapi_reset();
	clock_reset();
	psyq_libetc_reset();
	CHECK(ResetCallback() != 0); /* first call starts the interrupt environment */
	VSyncCallbacks(2, slot2);
	slot2_calls = 0;
	CHECK(ResetCallback() == 0); /* later calls change nothing */
	VSync(0);
	CHECK(slot2_calls == 1);
}

static void target_zero(void)
{
	reset();
	rcnt_hits = 0;
	InterruptCallback(6, on_rcnt2);
	SetRCnt(RCntCNT2, 0, RCntMdINTR);
	clock_advance(8 * 0x10000 - 1);
	CHECK(rcnt_hits == 0);
	clock_advance(1);
	CHECK(rcnt_hits == 1);
	/* ResetRCnt while armed restarts the period. */
	clock_advance(8 * 0x8000);
	ResetRCnt(RCntCNT2);
	clock_advance(8 * 0x10000 - 1);
	CHECK(rcnt_hits == 1);
	clock_advance(1);
	CHECK(rcnt_hits == 2);
}

static void *other_thread(void *arg)
{
	(void)arg;
	clock_advance(1);
	return NULL;
}

static void owner_thread(void)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		freopen("/dev/null", "w", stderr);
		setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }); /* expected abort: no core dump */
		reset();
		clock_advance(1); /* this thread owns the clock */
		pthread_t t;
		pthread_create(&t, NULL, other_thread, NULL);
		pthread_join(t, NULL);
		_exit(0);
	}
	int status;
	waitpid(pid, &status, 0);
	CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
	no_drift();
	vsync_semantics();
	polling_terminates();
	root_counter();
	late_root_counter();
	deterministic();
	same_cycle_order();
	cancel_ids();
	handler_calls_psyq();
	handler_wait_aborts();
	reset_callback_once();
	target_zero();
	owner_thread();
	printf("test_clock: %s\n", failures ? "FAIL" : "OK");
	return failures != 0;
}
