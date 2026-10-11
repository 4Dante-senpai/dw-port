#ifndef PORT_PS1MEM_H
#define PORT_PS1MEM_H

/*
 * PS1 memory model of the port (AD-3, AD-4, AD-5).
 *
 * - RAM: the 2 MB arena Ps1_Ram. PS1_RAM(addr) maps a PS1 address (KUSEG, KSEG0 or KSEG1) to it.
 * - Scratchpad: the 1 KB Ps1_Scratchpad, reached only through ps1_scratchpad (the port's PsyQ
 *   headers route getScratchAddr to it).
 * - Ordering-table links: 24-bit offsets (below the 0xFFFFFF end marker) into a 16 MB window that holds every object an OT can
 *   point to (globals, arena, primitive buffers, the game thread stack). PS1_LINK builds a link
 *   and PS1_LINK_PTR turns one back into a pointer; nothing else may.
 */

#include <stddef.h>
#include <stdint.h>

#include "port.h"

_Static_assert(sizeof(void *) == 4, "the port requires 32-bit pointers (native is built with -m32)");

#define PS1_RAM_SIZE 0x200000u /* PS1_RAM_ADDR in include/port_game.h masks with 0x1FFFFF */
#define PS1_SCRATCHPAD_SIZE 0x400u
#define PS1_LINK_END 0xFFFFFFu /* PsyQ end-of-OT marker */

#ifdef __EMSCRIPTEN__
#define PS1_WINDOW_BASE 0x00000000u
#else
#define PS1_WINDOW_BASE 0x01000000u /* native image linked with -Wl,-Ttext-segment=0x01000000 */
#endif
#define PS1_WINDOW_SIZE 0x01000000u

extern uint8_t Ps1_Ram[PS1_RAM_SIZE];
extern uint8_t Ps1_Scratchpad[PS1_SCRATCHPAD_SIZE];

static inline int ps1_addr_is_ram(uint32_t addr)
{
	uint32_t seg = addr & 0xE0000000u;
	return (seg == 0x00000000u || seg == 0x80000000u || seg == 0xA0000000u) &&
	       (addr & 0x1FFFFFFFu) < PS1_RAM_SIZE;
}

static inline void *ps1_ram(uint32_t addr)
{
	PORT_ASSERT(ps1_addr_is_ram(addr));
	return Ps1_Ram + (addr & (PS1_RAM_SIZE - 1));
}

static inline void *ps1_scratchpad(uint32_t offset)
{
	PORT_ASSERT(offset < PS1_SCRATCHPAD_SIZE);
	return Ps1_Scratchpad + offset;
}

static inline int ps1_in_window(const void *p)
{
	return (uintptr_t)p - PS1_WINDOW_BASE < PS1_WINDOW_SIZE;
}

static inline uint32_t ps1_link(const void *p)
{
	/* NULL is never a link (it is inside the web window, whose base is 0). */
	PORT_ASSERT(p != NULL && ps1_in_window(p) && (uintptr_t)p - PS1_WINDOW_BASE < PS1_LINK_END);
	return (uint32_t)((uintptr_t)p - PS1_WINDOW_BASE);
}

static inline void *ps1_link_ptr(uint32_t link)
{
	PORT_ASSERT(link < PS1_LINK_END);
	return (void *)(uintptr_t)(PS1_WINDOW_BASE + link);
}

#define PS1_RAM(addr) ps1_ram((uint32_t)(addr))
#define PS1_LINK(ptr) ps1_link(ptr)
#define PS1_LINK_PTR(link) ps1_link_ptr(link)

#endif
