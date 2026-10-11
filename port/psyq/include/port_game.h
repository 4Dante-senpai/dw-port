#ifndef PORT_GAME_H
#define PORT_GAME_H

/* Force-included into every game TU (CMake). Pins the compiler semantics of AD-17. */

_Static_assert((char)-1 < 0, "the game requires signed char (AD-17, docs/port/char-signedness.md)");
_Static_assert(sizeof(void *) == 4, "the game requires 32-bit pointers (AD-3)");

#endif
