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
#include <libspu.h>
#include <libsnd.h>
#include <libpress.h>
#include <libmcrd.h>
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

_Static_assert(sizeof(GsCOORD2PARAM)==40 && sizeof(GsCOORDINATE2)==80 &&
               offsetof(GsCOORDINATE2,coord)==4 && offsetof(GsCOORDINATE2,workm)==36 &&
               offsetof(GsCOORDINATE2,super)==72, "libgs coordinate ABI");
_Static_assert(sizeof(GsVIEW2)==36 && sizeof(GsRVIEW2)==32 && sizeof(GsF_LIGHT)==16,
               "libgs view and light ABI");
_Static_assert(sizeof(GsOT_TAG)==4 && sizeof(GsOT)==20 && offsetof(GsOT,tag)==16 &&
               sizeof(GsDOBJ2)==16 && offsetof(GsDOBJ2,tmd)==8, "libgs OT and object ABI");
_Static_assert(sizeof(GsSPRITE)==36 && offsetof(GsSPRITE,mx)==24 &&
               offsetof(GsSPRITE,rotate)==32 && sizeof(GsBOXF)==16 && sizeof(GsIMAGE)==28 &&
               offsetof(GsIMAGE,pixel)==12 && offsetof(GsIMAGE,clut)==24, "libgs 2D ABI");
_Static_assert(sizeof(_GsFCALL)==0x130 && offsetof(_GsFCALL,tf3)==0x40 &&
               offsetof(_GsFCALL,nf4)==0x98 && offsetof(_GsFCALL,f3g)==0x100,
               "libgs function table ABI");
_Static_assert(sizeof(TMD_P_F3)==16, "TMD F3 ABI");
_Static_assert(sizeof(TMD_P_F4)==20, "TMD F4 ABI");
_Static_assert(sizeof(TMD_P_G3)==20, "TMD G3 ABI");
_Static_assert(sizeof(TMD_P_G4)==24, "TMD G4 ABI");
_Static_assert(sizeof(TMD_P_NF3)==16, "TMD NF3 ABI");
_Static_assert(sizeof(TMD_P_NF4)==16, "TMD NF4 ABI");
_Static_assert(sizeof(TMD_P_NG3)==24, "TMD NG3 ABI");
_Static_assert(sizeof(TMD_P_NG4)==28, "TMD NG4 ABI");
_Static_assert(sizeof(TMD_P_TF3)==24, "TMD TF3 ABI");
_Static_assert(sizeof(TMD_P_TF4)==32, "TMD TF4 ABI");
_Static_assert(sizeof(TMD_P_TG3)==28, "TMD TG3 ABI");
_Static_assert(sizeof(TMD_P_TG4)==36, "TMD TG4 ABI");
_Static_assert(sizeof(TMD_P_TNF3)==28, "TMD TNF3 ABI");
_Static_assert(sizeof(TMD_P_TNF4)==32, "TMD TNF4 ABI");
_Static_assert(sizeof(TMD_P_TNG3)==36, "TMD TNG3 ABI");
_Static_assert(sizeof(TMD_P_TNG4)==44, "TMD TNG4 ABI");

_Static_assert(sizeof(TMD_P_F3G)==24 && sizeof(TMD_P_G3G)==28 &&
               sizeof(TMD_P_F4G)==32 && sizeof(TMD_P_G4G)==36, "TMD gradation ABI");
_Static_assert(__builtin_offsetof(TMD_P_F3,n0)==8 && __builtin_offsetof(TMD_P_F4,v3)==16 &&
               __builtin_offsetof(TMD_P_TF4,n0)==20 && __builtin_offsetof(TMD_P_TG4,v3)==34 &&
               __builtin_offsetof(TMD_P_TNF3,v0)==20 && __builtin_offsetof(TMD_P_TNG4,v0)==36,
               "TMD vertex and normal offsets");

_Static_assert(sizeof(StHEADER) == 32 && offsetof(StHEADER, frameCount) == 8 &&
               offsetof(StHEADER, width) == 16 && offsetof(StHEADER, loc) == 28,
               "stream header ABI");
_Static_assert(sizeof(SpuReverbAttr) == 20 && offsetof(SpuReverbAttr, depth) == 8,
               "SPU reverb ABI");
_Static_assert(SPU_OFF == 0 && McFuncExist == 1 && McFuncAccept == 2 &&
               McFuncReadFile == 3 && McFuncWriteFile == 4 && McErrCardNotExist == 1,
               "silent audio and absent card constants");
