/* Compile-only check: the port's PsyQ prototypes (psyq.h) agree with the mw_lib41 headers the
 * game compiles against; a mismatch is a conflicting-declaration error. */
#include <sys/types.h>
#include <kernel.h>
#include <libapi.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>

#include "../psyq.h"

_Static_assert(RCntCNT0 == 0xF2000000 && RCntCNT2 == 0xF2000002, "kernel.h root counter ids");
_Static_assert(RCntMdINTR == 0x1000 && RCntMdNOINTR == 0x2000 && RCntMdSC == 0x0001,
               "kernel.h root counter modes");
