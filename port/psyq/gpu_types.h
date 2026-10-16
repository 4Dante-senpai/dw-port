/* libgpu.h ABI: native i686 and wasm32, checked against mw_lib41. */
#ifndef PORT_GPU_TYPES_H
#define PORT_GPU_TYPES_H
#include <stddef.h>
#ifndef _LIBGPU_H_
typedef struct { short x, y, w, h; } RECT;
typedef struct { unsigned long tag, code[15]; } DR_ENV;
typedef struct {
    RECT clip; short ofs[2]; RECT tw; unsigned short tpage;
    unsigned char dtd, dfe, isbg, r0, g0, b0; DR_ENV dr_env;
} DRAWENV;
typedef struct { RECT disp, screen; unsigned char isinter, isrgb24, pad0, pad1; } DISPENV;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code;
    short x0,y0,x1,y1,x2,y2,x3,y3; } POLY_F4;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code;
    short x0,y0; unsigned char u0,v0; unsigned short clut;
    short x1,y1; unsigned char u1,v1; unsigned short tpage;
    short x2,y2; unsigned char u2,v2; unsigned short pad1; } POLY_FT3;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code;
    short x0,y0; unsigned char u0,v0; unsigned short clut;
    short x1,y1; unsigned char u1,v1; unsigned short tpage;
    short x2,y2; unsigned char u2,v2; unsigned short pad1;
    short x3,y3; unsigned char u3,v3; unsigned short pad2; } POLY_FT4;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code; short x0,y0;
    unsigned char r1,g1,b1,pad1; short x1,y1;
    unsigned char r2,g2,b2,pad2; short x2,y2;
    unsigned char r3,g3,b3,pad3; short x3,y3; } POLY_G4;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code; short x0,y0;
    unsigned char u0,v0; unsigned short clut;
    unsigned char r1,g1,b1,p1; short x1,y1; unsigned char u1,v1; unsigned short tpage;
    unsigned char r2,g2,b2,p2; short x2,y2; unsigned char u2,v2; unsigned short pad2;
    unsigned char r3,g3,b3,p3; short x3,y3; unsigned char u3,v3; unsigned short pad3; } POLY_GT4;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code; short x0,y0,x1,y1; } LINE_F2;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code; short x0,y0,x1,y1,x2,y2;
    unsigned long pad; } LINE_F3;
typedef struct { unsigned long tag; unsigned char r0,g0,b0,code; short x0,y0,x1,y1,x2,y2,x3,y3;
    unsigned long pad; } LINE_F4;
typedef struct { unsigned long tag, code[2]; } DR_OFFSET;
typedef struct { unsigned long tag, code[3], p[13]; } DR_LOAD;
typedef struct { unsigned long mode; RECT *crect; unsigned long *caddr;
    RECT *prect; unsigned long *paddr; } TIM_IMAGE;
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

#endif
#endif
