#ifndef __EMSCRIPTEN__
#define _GNU_SOURCE
#include "death.h"
#include "playtest.h"
#include "state.h"
#include <execinfo.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

#define MAX_FRAMES 64

static FILE *out;
static int dumped, quit_forward;
static pthread_t game;
static const void *signal_pc; /* where the signal interrupted the thread: an exact pc, not a return address */

void port_death_game_thread(pthread_t thread) { game = thread; quit_forward = 1; }

static char out_buffer[BUFSIZ];

/* Best effort from signal context: the process is already dying. The FILE and its static buffer
 * are set up at install, so the dump itself does not allocate in stdio. */
void port_death_dump(const char *reason, int sig, const void *addr)
{
    if (!out || __sync_lock_test_and_set(&dumped, 1)) return;
    void *frames[MAX_FRAMES];
    int n = backtrace(frames, MAX_FRAMES);
    fprintf(out, "dw1-death 1\nreason %s\nsignal %d\nfault_address 0x%08lx\nvblank %u\n", reason, sig,
            (unsigned long)(uintptr_t)addr, (unsigned)port_playtest_vblank());
    port_state_header(out);
    port_state_record(out, port_playtest_vblank(), 0);
    uint32_t k[PORT_PLAYTEST_RING];
    uint16_t mask[PORT_PLAYTEST_RING];
    unsigned inputs = port_playtest_recent(PORT_PLAYTEST_RING, k, mask);
    for (unsigned i = 0; i < inputs; i++) fprintf(out, "input %u %04x\n", (unsigned)k[i], mask[i]);
    if (signal_pc) fprintf(out, "pc 0x%08lx\n", (unsigned long)(uintptr_t)signal_pc);
    for (int i = 0; i < n; i++) fprintf(out, "frame 0x%08lx\n", (unsigned long)(uintptr_t)frames[i]);
    fflush(out);
}

static void note_pc(void *context)
{
#ifdef __i386__
	signal_pc = (const void *)(uintptr_t)((ucontext_t *)context)->uc_mcontext.gregs[REG_EIP];
#else
	(void)context;
#endif
}

#if !__has_feature(address_sanitizer) /* ASan reports crashes better */
/* A crash in a windowed session leaves no trace otherwise: print the faulting address and the raw
 * frames, then die with the same signal. Resolve them with llvm-symbolizer-18 --obj=dw1: GNU
 * addr2line 2.42 names the generated sjis/ copy instead of the #line source of clang's DWARF 5. */
static void crash(int sig, siginfo_t *info, void *context)
{
	note_pc(context);
	void *frames[MAX_FRAMES];
	char line[96];
	int n = snprintf(line, sizeof(line), "dw1: signal %d at address %p; backtrace:\n", sig, info->si_addr);
	if (n > 0) (void)!write(2, line, (size_t)n);
	backtrace_symbols_fd(frames, backtrace(frames, MAX_FRAMES), 2);
	port_death_dump(sig == SIGSEGV ? "segv" : sig == SIGBUS ? "bus" : sig == SIGILL ? "ill" : "fpe", sig, info->si_addr);
	signal(sig, SIG_DFL);
	raise(sig);
}
#else
extern void __asan_set_death_callback(void (*callback)(void));
static void sanitizer_died(void) { port_death_dump("sanitizer", 0, NULL); }
#endif

static void aborted(int sig, siginfo_t *info, void *context)
{
	(void)info;
	note_pc(context);
	port_death_dump("abort", sig, NULL);
	signal(sig, SIG_DFL);
	raise(sig);
}

static void quit(int sig, siginfo_t *info, void *context)
{
	(void)info;
	/* The runner signals the process: report the game thread's frames, not whichever thread caught it. */
	if (quit_forward && !pthread_equal(pthread_self(), game)) {
		if (pthread_kill(game, sig) == 0) return; /* the game thread is gone: dump from here */
	}
	note_pc(context);
	port_death_dump("quit", sig, NULL);
	_exit(128 + sig);
}

void port_death_install(int fd)
{
	void *warm[1];
	(void)backtrace(warm, 1); /* loads libgcc now, not inside a handler */
#if !__has_feature(address_sanitizer)
	struct sigaction action = {.sa_sigaction = crash, .sa_flags = SA_SIGINFO};
	sigaction(SIGSEGV, &action, NULL);
	sigaction(SIGBUS, &action, NULL);
	sigaction(SIGILL, &action, NULL);
	sigaction(SIGFPE, &action, NULL);
#endif
	if (fd < 0) return;
	out = fdopen(fd, "w");
	if (!out) { perror("dw1: death file"); return; }
	setvbuf(out, out_buffer, _IOFBF, sizeof(out_buffer));
	struct sigaction abort_action = {.sa_sigaction = aborted, .sa_flags = SA_SIGINFO};
	struct sigaction quit_action = {.sa_sigaction = quit, .sa_flags = SA_SIGINFO};
	sigaction(SIGABRT, &abort_action, NULL);
	sigaction(SIGQUIT, &quit_action, NULL);
#if __has_feature(address_sanitizer)
	__asan_set_death_callback(sanitizer_died);
#endif
}
#endif
