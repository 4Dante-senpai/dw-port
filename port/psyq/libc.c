/* BIOS libc entries used by the game. asm:asm/us/main/psyq/libc.s */
#include <stdint.h>

#include "psyq.h"

/* OpenBIOS nugget 361f3f6a: kernel/libcmisc.c zero-initializes s_currentSeed.
 * Provisional until the owner BIOS comparison. Verified return trace: docs/port/reference.md. */
__attribute__((weak)) void port_rand_trace(int value);
#define BIOS_RAND_SEED 0u
static uint32_t rand_state = BIOS_RAND_SEED;

/* asm:asm/us/main/psyq/libc.s:rand -- BIOS A(2Fh). Unsigned arithmetic wraps at 32 bits. */
int rand(void)
{
	rand_state = rand_state * 0x41C64E6Du + 0x3039u;
	int value = (int)((rand_state >> 16) & 0x7FFFu);
	if (port_rand_trace) port_rand_trace(value);
	return value;
}

/* asm:asm/us/main/psyq/libc.s:rand uses the state seeded by BIOS A(30h), srand.
 * DW1 does not call this entry; exposed for deterministic tests. */
void srand(unsigned int seed)
{
	rand_state = seed;
}

/* asm: BIOS startup state for A(2Fh), using the provisional OpenBIOS seed above. */
void psyq_libc_reset(void)
{
	rand_state = BIOS_RAND_SEED;
}
