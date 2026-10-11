/* L1: PS1 memory model (AD-4, AD-5). */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../ps1mem.h"

static int failures;
#define CHECK(cond)                                                                        \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
			failures++;                                                                    \
		}                                                                                  \
	} while (0)

/* Runs fn in a child process and reports whether it aborted (PORT_ASSERT). */
static int aborts(void (*fn)(uint32_t), uint32_t arg)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(2);
	}
	if (pid == 0) {
		freopen("/dev/null", "w", stderr);
		setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }); /* expected abort: no core dump */
		fn(arg);
		_exit(0);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void ram(uint32_t a) { (void)PS1_RAM(a); }
static void scratch(uint32_t o) { (void)ps1_scratchpad(o); }
static void link_of(uint32_t p) { (void)PS1_LINK((void *)(uintptr_t)p); }
static void ptr_of(uint32_t l) { (void)PS1_LINK_PTR(l); }

extern char _end[]; /* end of the image (.bss), from the linker */

static int global_in_image = 1;
static uint8_t thread_stack[64 * 1024] __attribute__((aligned(16)));
static void *thread_local_addr;

static void *thread_main(void *arg)
{
	int local = 0;
	(void)arg;
	thread_local_addr = &local;
	return NULL;
}

int main(void)
{
	/* PS1_RAM */
	CHECK(PS1_RAM(0x80010000) == Ps1_Ram + 0x10000);
	CHECK(PS1_RAM(0x00010000) == Ps1_Ram + 0x10000);
	CHECK(PS1_RAM(0xA01FFFFF) == Ps1_Ram + 0x1FFFFF);
	CHECK(!aborts(ram, 0x801FFFFF));
	CHECK(aborts(ram, 0x80200000));
	CHECK(aborts(ram, 0x1F800000));
	CHECK(aborts(ram, 0xC0010000)); /* KSEG2: valid offset, wrong segment */
	CHECK(aborts(ram, 0x20010000));

	/* scratchpad */
	CHECK(ps1_scratchpad(0x3FF) == Ps1_Scratchpad + 0x3FF);
	CHECK(aborts(scratch, 0x400));

	/* OT links */
	CHECK(PS1_LINK_PTR(PS1_LINK(&Ps1_Ram[0x1234])) == &Ps1_Ram[0x1234]);
	CHECK(PS1_LINK(&Ps1_Ram[0]) < PS1_LINK_END);
	CHECK(aborts(link_of, PS1_WINDOW_BASE + PS1_WINDOW_SIZE));
	CHECK(aborts(link_of, PS1_WINDOW_BASE + PS1_LINK_END)); /* would equal the end marker */
	CHECK(aborts(link_of, PS1_WINDOW_BASE - 1));
	CHECK(aborts(link_of, 0)); /* NULL */
	CHECK(aborts(ptr_of, PS1_LINK_END));
	CHECK(aborts(ptr_of, PS1_WINDOW_SIZE));

	/* the window holds globals, the arena and a game-thread stack from a static buffer */
	pthread_attr_t attr;
	pthread_t thread;
	pthread_attr_init(&attr);
	CHECK(pthread_attr_setstack(&attr, thread_stack, sizeof thread_stack) == 0);
	CHECK(pthread_create(&thread, &attr, thread_main, NULL) == 0);
	pthread_join(thread, NULL);
	CHECK(ps1_in_window(&global_in_image));
	CHECK(ps1_in_window(Ps1_Ram) && ps1_in_window(Ps1_Ram + PS1_RAM_SIZE - 1));
	CHECK(ps1_in_window(Ps1_Scratchpad));
	CHECK(ps1_in_window(_end - 1)); /* the whole image, .bss included */
	CHECK(ps1_in_window(thread_local_addr));
	CHECK((uint8_t *)thread_local_addr >= thread_stack &&
	      (uint8_t *)thread_local_addr < thread_stack + sizeof thread_stack);

	printf("test_ps1mem: %s\n", failures ? "FAIL" : "OK");
	return failures != 0;
}
