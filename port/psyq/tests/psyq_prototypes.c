/* Compile-only check: the port's PsyQ prototypes (psyq.h) agree with the mw_lib41 headers the
 * game compiles against; a mismatch is a conflicting-declaration error. */
#include <sys/types.h>
#include <kernel.h>
#include <libapi.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <libcd.h>

#include "../psyq.h"

_Static_assert(RCntCNT0 == 0xF2000000 && RCntCNT2 == 0xF2000002, "kernel.h root counter ids");
_Static_assert(RCntMdINTR == 0x1000 && RCntMdNOINTR == 0x2000 && RCntMdSC == 0x0001,
               "kernel.h root counter modes");
_Static_assert(CdlNop == 1 && CdlSetloc == 2 && CdlPause == 9 && CdlSetmode == 0x0e &&
               CdlComplete == 2, "libcd.h commands");
_Static_assert(CdlModeSpeed == 0x80 && CdlModeSize1 == 0x20 && CdlModeSize0 == 0x10,
               "libcd.h modes");
