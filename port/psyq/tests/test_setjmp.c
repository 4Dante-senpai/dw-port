/* L1: with port_game.h, a game setjmp into PsyQ's 48-byte jmp_buf writes inside it and
 * longjmp returns to it. Built like a game TU (mw_lib41 setjmp.h, forced port_game.h). */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static struct {
	jmp_buf buf;
	unsigned char canary[256];
} guarded;

int main(void)
{
	memset(guarded.canary, 0xA5, sizeof guarded.canary);
	volatile int passes = 0;
	switch (setjmp(guarded.buf)) {
	case 0:
		passes++;
		longjmp(guarded.buf, 7);
		return 1; /* longjmp must not return to its caller. */
	case 7:
		break;
	default:
		printf("test_setjmp: FAIL (unexpected longjmp value)\n");
		return 1;
	}
	passes++;
	int ok = sizeof(jmp_buf) == 48 && passes == 2;
	for (size_t i = 0; i < sizeof guarded.canary; i++)
		ok = ok && guarded.canary[i] == 0xA5;
	printf("test_setjmp: %s\n", ok ? "OK" : "FAIL");
	return !ok;
}
