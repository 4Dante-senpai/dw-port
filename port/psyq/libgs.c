/* libgs (AD-2). asm:asm/us/main/psyq/libgs.s (mw_lib41); the rest arrives in story 2.13. */
#include "clock.h"
#include "psyq.h"

PACKET *GsOUT_PACKET_P; /* current primitive work area */

/* asm: GsSetWorkBase */
void GsSetWorkBase(PACKET *outpacketp)
{
	clock_psyq_entry();
	GsOUT_PACKET_P = outpacketp;
}

/* asm: GsGetWorkBase */
PACKET *GsGetWorkBase(void)
{
	clock_psyq_entry();
	return GsOUT_PACKET_P;
}
