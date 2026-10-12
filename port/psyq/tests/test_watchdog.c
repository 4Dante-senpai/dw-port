/* A game that never ends a frame: the runtime's watchdog must stop it with status 3. */
#include "../clock.h"
#include "../crt.h"

int dw1_game_main(void)
{
	for (;;)
		clock_psyq_entry(); /* waits on something no stub provides */
}

int main(void)
{
	return port_run_game(1);
}
