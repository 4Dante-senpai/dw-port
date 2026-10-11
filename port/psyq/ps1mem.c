#include "ps1mem.h"

_Static_assert(PS1_RAM_SIZE == 0x200000u, "PS1_RAM_ADDR (include/port_game.h) assumes 2 MB");

uint8_t Ps1_Ram[PS1_RAM_SIZE];
uint8_t Ps1_Scratchpad[PS1_SCRATCHPAD_SIZE];
