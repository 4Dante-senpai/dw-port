#ifndef PORT_GAME_H
#define PORT_GAME_H

/* Force-included into every game TU (CMake). Pins the compiler semantics of AD-17. */

_Static_assert((char)-1 < 0, "the game requires signed char (AD-17, docs/port/char-signedness.md)");
_Static_assert(sizeof(void *) == 4, "the game requires 32-bit pointers (AD-3)");

/*
 * PS1 RAM address as a pointer into the port's arena (AD-4). A constant expression, so it can
 * initialize static data; used only by the DW1_PORT blocks of src/ and include/, whose literals
 * tools/port/metrics/ps1_literals.py checks (no assert here: it must stay constant).
 */
extern unsigned char Ps1_Ram[];
/* The sizeof term fails to compile when addr is not KSEG0 RAM (0x80000000-0x801FFFFF). */
#define PS1_RAM_ADDR(addr)                                                                  \
	((void *)(Ps1_Ram + ((addr) & 0x1FFFFFu) +                                              \
	          0 * sizeof(char[((addr) & 0xFFE00000u) == 0x80000000u ? 1 : -1])))

/*
 * PsyQ's setjmp/longjmp use a 48-byte jmp_buf (mw_lib41 setjmp.h). glibc's _setjmp/_longjmp on
 * i686 store 28 bytes (no signal mask), so the game's buffers are large enough; the full setjmp
 * would save the signal mask past their end (tests/test_setjmp.c). [hypothesis for web: checked
 * when the Emscripten build exists, story 4.1]
 */
#ifndef __EMSCRIPTEN__
#define setjmp _setjmp
#define longjmp _longjmp
#endif

#endif
