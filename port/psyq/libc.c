/* BIOS libc entries used by the game. asm:asm/us/main/psyq/libc.s */
#include <stdint.h>

#include "psyq.h"

/* [hypothesis] BIOS boot seed from PCSX-ReARMed HLE; verify against the reference emulator
 * in Epic 3. These BIOS calls have no modeled clock cost. */
#define BIOS_RAND_SEED 0x24040001u
static uint32_t rand_state = BIOS_RAND_SEED;

/* asm:asm/us/main/psyq/libc.s:rand -- BIOS A(2Fh). Unsigned arithmetic wraps at 32 bits. */
int rand(void)
{
	rand_state = rand_state * 0x41C64E6Du + 0x3039u;
	return (int)((rand_state >> 16) & 0x7FFFu);
}

/* asm:asm/us/main/psyq/libc.s:rand uses the state seeded by BIOS A(30h), srand.
 * DW1 does not call this entry; exposed for deterministic tests. */
void srand(unsigned int seed)
{
	rand_state = seed;
}

/* asm: BIOS startup state for A(2Fh), with the seed hypothesis above. */
void psyq_libc_reset(void)
{
	rand_state = BIOS_RAND_SEED;
}
