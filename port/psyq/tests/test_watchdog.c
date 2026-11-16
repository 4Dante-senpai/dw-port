/* A game that never ends a frame: the runtime's watchdog must stop it with status 3. */
#include <stddef.h>
#include <stdlib.h>
#include "../psyq.h"
#include "../clock.h"
#include "../crt.h"

int dw1_game_main(void)
{
	if (getenv("WATCHDOG_PAD_POLL")) { /* the pause loop: PadRead with no VSync is not a stalled stub */
		while (clock_now() < 100ull * CLOCK_HZ)
			PadRead(1);
		exit(0);
	}
	for (;;)
		clock_psyq_entry(); /* waits on something no stub provides */
}

int main(void)
{
	return port_run_game(1, 0, NULL);
}
