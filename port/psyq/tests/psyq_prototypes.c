/* Compile-only check: the port's PsyQ prototypes (psyq.h) agree with the mw_lib41 headers the
 * game compiles against; a mismatch is a conflicting-declaration error. */
#include <stddef.h>
#include <sys/types.h>
#include <kernel.h>
#include <libapi.h>
#include <libetc.h>
#include <libgte.h>
#include <libgpu.h>
#include <libgs.h>
#include <libcd.h>
#include <malloc.h>
#include <rand.h>

#include "../psyq.h"

/* mw_lib41 stddef.h omits offsetof; use the compiler intrinsic in this ABI check. */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif

_Static_assert(RCntCNT0 == 0xF2000000 && RCntCNT2 == 0xF2000002, "kernel.h root counter ids");
_Static_assert(RCntMdINTR == 0x1000 && RCntMdNOINTR == 0x2000 && RCntMdSC == 0x0001,
               "kernel.h root counter modes");
_Static_assert(CdlNop == 1 && CdlSetloc == 2 && CdlPause == 9 && CdlSetmode == 0x0e &&
               CdlComplete == 2, "libcd.h commands");
_Static_assert(CdlModeSpeed == 0x80 && CdlModeSize1 == 0x20 && CdlModeSize0 == 0x10,
               "libcd.h modes");

_Static_assert(sizeof(RECT) == 8 && sizeof(DISPENV) == 20 && sizeof(DRAWENV) == 92,
               "libgpu environment ABI");
_Static_assert(sizeof(POLY_FT3) == 32 && sizeof(POLY_FT4) == 40 && sizeof(POLY_F4) == 24 &&
               sizeof(POLY_G4) == 36 && sizeof(POLY_GT4) == 52, "libgpu polygon ABI");
_Static_assert(sizeof(LINE_F2) == 16 && sizeof(LINE_F3) == 24 && sizeof(LINE_F4) == 28 &&
               sizeof(DR_LOAD) == 68 && sizeof(DR_OFFSET) == 12 && sizeof(TIM_IMAGE) == 20,
               "libgpu helper ABI");

/* Literal field offsets must agree with mw_lib41 as well as total sizes. */
_Static_assert(offsetof(DRAWENV, dr_env) == 28 && offsetof(DRAWENV, tpage) == 20 &&
               offsetof(DRAWENV, isbg) == 24, "libgpu draw environment offsets");
_Static_assert(offsetof(POLY_FT4, u0) == 12 && offsetof(POLY_FT4, clut) == 14 &&
               offsetof(POLY_FT4, tpage) == 22, "libgpu flat textured polygon offsets");
_Static_assert(offsetof(POLY_GT4, u0) == 12 && offsetof(POLY_GT4, clut) == 14 &&
               offsetof(POLY_GT4, tpage) == 26, "libgpu Gouraud textured polygon offsets");
_Static_assert(offsetof(POLY_G4, r1) == 12 && offsetof(LINE_F3, pad) == 20 &&
               offsetof(LINE_F4, pad) == 24 && offsetof(DR_LOAD, p) == 16,
               "libgpu packet field offsets");
_Static_assert(offsetof(TIM_IMAGE, mode) == 0 && offsetof(TIM_IMAGE, crect) == 4 &&
               offsetof(TIM_IMAGE, caddr) == 8 && offsetof(TIM_IMAGE, prect) == 12 &&
               offsetof(TIM_IMAGE, paddr) == 16, "libgpu TIM field offsets");
