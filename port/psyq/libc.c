/* BIOS libc entries used by the game. asm:asm/us/main/psyq/libc.s */
#include <stdint.h>
#include <string.h>

#include "psyq.h"
#include "clock.h"

void *psyq_memcpy(void *dst, const void *src, size_t size)
{
	clock_advance_cost(CLOCK_LIBC_MEMCPY_FIXED_CYCLES +
	                   (uint64_t)size * CLOCK_LIBC_MEMCPY_BYTE_CYCLES, CLOCK_COST_CPU);
	return memcpy(dst, src, size);
}

void *psyq_memset(void *dst, int value, size_t size)
{
	clock_advance_cost(CLOCK_LIBC_MEMSET_FIXED_CYCLES +
	                   (uint64_t)size * CLOCK_LIBC_MEMSET_BYTE_CYCLES, CLOCK_COST_CPU);
	return memset(dst, value, size);
}

int psyq_strcmp(const char *left, const char *right)
{
	size_t n = 0;
	while (left[n] && left[n] == right[n]) n++;
	int result = (signed char)left[n] - (signed char)right[n];
	clock_advance_cost(CLOCK_LIBC_STRCMP_FIXED_CYCLES +
	                   (uint64_t)n * CLOCK_LIBC_STRCMP_CHAR_CYCLES +
	                   (!result ? CLOCK_LIBC_STRCMP_EQUAL_CYCLES : 0), CLOCK_COST_CPU);
	return result;
}

char *psyq_strrchr(const char *text, int value)
{
	const char *last = NULL;
	size_t n = 0, matches = 0;
	for (; text[n]; n++) {
		if ((unsigned char)text[n] == (unsigned char)value) { last = text + n; matches++; }
	}
	if ((unsigned char)value == 0) last = text + n;
	clock_advance_cost(CLOCK_LIBC_STRRCHR_FIXED_CYCLES +
	                   (uint64_t)n * CLOCK_LIBC_STRRCHR_CHAR_CYCLES +
	                   (uint64_t)matches * CLOCK_LIBC_STRRCHR_MATCH_CYCLES, CLOCK_COST_CPU);
	return (char *)last;
}

/* SCPH-1001 startup seed, derived 206 calls before the natural New Game trace.
 * Derivation and matching OpenBIOS patch: docs/port/reference.md. */
__attribute__((weak)) void port_rand_trace(int value);
#define BIOS_RAND_SEED 0x24040001u
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

/* asm: BIOS startup state for A(2Fh), using the measured SCPH-1001 seed above. */
void psyq_libc_reset(void)
{
	rand_state = BIOS_RAND_SEED;
}
