/* L1: built like a game TU (port headers first, mw_lib41 behind), getScratchAddr reaches the
 * port's scratchpad instead of the PS1 hardware address. */
#include <stdio.h>
#include <sys/types.h>

#include <libetc.h>

unsigned char Ps1_Scratchpad[0x400];

int main(void)
{
	u_long *word = getScratchAddr(1 + 1);

	if ((unsigned char *)getScratchAddr(0) != Ps1_Scratchpad || (unsigned char *)word != Ps1_Scratchpad + 8) {
		printf("test_scratchpad: FAIL\n");
		return 1;
	}
	*word = 0x12345678;
	printf("test_scratchpad: OK\n");
	return 0;
}
