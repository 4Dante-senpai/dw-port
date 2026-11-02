#ifndef PORT_GAME_LIBETC_H
#define PORT_GAME_LIBETC_H
#include_next <libetc.h>

/* PsyQ's macro is the hardware address 0x1F800000, unmapped in the port (AD-4). */
extern unsigned char Ps1_Scratchpad[];
#undef getScratchAddr
#define getScratchAddr(offset) ((u_long *)(Ps1_Scratchpad + (offset) * 4))
#endif
