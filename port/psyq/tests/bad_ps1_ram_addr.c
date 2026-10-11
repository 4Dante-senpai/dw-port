/* Must NOT compile: PS1_RAM_ADDR rejects addresses outside KSEG0 RAM at compile time. */
#include "port_game.h"

void *bad = PS1_RAM_ADDR(0x80200000u);
