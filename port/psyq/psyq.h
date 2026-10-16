#ifndef PORT_PSYQ_H
#define PORT_PSYQ_H

#include <stddef.h>

/*
 * Prototypes of the PsyQ functions the port implements, as in the mw_lib41 headers the game
 * compiles against (external/psyq_headers/mw_lib41/include), plus the port's internal hooks.
 */


/* libgpu.h */
#include "gpu_types.h"
DISPENV *PutDispEnv(DISPENV *env);
DRAWENV *PutDrawEnv(DRAWENV *env);
DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h);
TIM_IMAGE *ReadTIM(TIM_IMAGE *image);
int ClearImage(RECT *rect, unsigned char r, unsigned char g, unsigned char b);
int DrawSync(int mode);
int LoadImage(RECT *rect, unsigned long *src);
int StoreImage(RECT *rect, unsigned long *dst);
int MoveImage(RECT *rect, int x, int y);
int OpenTIM(unsigned long *addr);
int ResetGraph(int mode);
int SetGraphDebug(int level);
unsigned long *ClearOTagR(unsigned long *ot, int n);
unsigned short GetClut(int x, int y);
unsigned short GetTPage(int tp, int abr, int x, int y);
unsigned short LoadClut(unsigned long *src, int x, int y);
void AddPrim(void *ot, void *prim);
void DrawOTag(unsigned long *ot);
void SetDispMask(int on);
void SetDrawLoad(DR_LOAD *p, RECT *rect);
void SetDrawOffset(DR_OFFSET *p, unsigned short *ofs);
void SetSemiTrans(void *p, int on);
void SetPolyFT3(POLY_FT3 *p);
void SetPolyFT4(POLY_FT4 *p);
void SetPolyF4(POLY_F4 *p);
void SetPolyG4(POLY_G4 *p);
void SetPolyGT4(POLY_GT4 *p);
void SetLineF2(LINE_F2 *p);
void SetLineF3(LINE_F3 *p);
void SetLineF4(LINE_F4 *p);

/* libetc.h */
void PadInit(int mode);
unsigned long PadRead(int id);
int VSync(int mode);
int VSyncCallback(void (*f)());
long VSyncCallbacks(int ch, void (*f)());
long InterruptCallback(int irq, void (*f)());
int ResetCallback(void);

/* libgs.h */
typedef unsigned char PACKET;
void GsSetWorkBase(PACKET *outpacketp);
PACKET *GsGetWorkBase(void);

/* libapi.h */
long SetRCnt(unsigned long spec, unsigned short target, long mode);
long GetRCnt(unsigned long spec);
long ResetRCnt(unsigned long spec);

/* malloc.h */
void InitHeap3(unsigned long *addr, unsigned long size);
void *malloc3(size_t size);
void free3(void *ptr);

/* rand.h */
int rand(void);
void srand(unsigned int seed);

/* kernel.h (values checked against it by tests/psyq_prototypes.c) */
#ifndef RCntCNT0
#define RCntCNT0 0xF2000000
#define RCntCNT1 0xF2000001
#define RCntCNT2 0xF2000002
#define RCntMdINTR 0x1000
#define RCntMdNOINTR 0x2000
#define RCntMdSC 0x0001
#define RCntMdSP 0x0000
#endif

/* libcd.h (types and values checked against it by tests/psyq_prototypes.c) */
#ifndef _LIBCD_H_
typedef struct {
	unsigned char minute, second, sector, track; /* BCD */
} CdlLOC;
typedef struct {
	CdlLOC pos;
	unsigned long size;
	char name[16];
} CdlFILE;
typedef void (*CdlCB)(unsigned char, unsigned char *);
#define CdlNop 0x01
#define CdlSetloc 0x02
#define CdlPause 0x09
#define CdlSetmode 0x0e
#define CdlComplete 0x02
#define CdlModeSpeed 0x80
#define CdlModeSize1 0x20
#define CdlModeSize0 0x10
#endif
int CdInit(void);
int CdControl(unsigned char com, unsigned char *param, unsigned char *result);
int CdControlB(unsigned char com, unsigned char *param, unsigned char *result);
int CdControlF(unsigned char com, unsigned char *param);
int CdSync(int mode, unsigned char *result);
int CdRead(int sectors, unsigned long *buf, int mode);
int CdReadSync(int mode, unsigned char *result);
CdlCB CdReadCallback(CdlCB func);
CdlFILE *CdSearchFile(CdlFILE *fp, char *name);
CdlLOC *CdIntToPos(int i, CdlLOC *p);
int CdPosToInt(CdlLOC *p);

/* libgte.h: native and wasm32 retain the PS1 layout. */
#ifndef _LIBGTE_H_
typedef struct { short m[3][3]; long t[3]; } MATRIX;
typedef struct { long vx, vy, vz, pad; } VECTOR;
typedef struct { short vx, vy, vz, pad; } SVECTOR;
typedef struct { unsigned char r, g, b, cd; } CVECTOR;
#endif
SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1);
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m);
MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m);
long ratan2(long y, long x);
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v);
MATRIX *TransMatrix(MATRIX *m, VECTOR *v);
int rsin(int a);
int rcos(int a);
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1);
void SetRotMatrix(MATRIX *m);
void SetTransMatrix(MATRIX *m);
void PopMatrix(void);
void PushMatrix(void);
MATRIX *MulMatrix0(MATRIX *m0, MATRIX *m1, MATRIX *m2);
long RotTransPers4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
			long *sxy0, long *sxy1, long *sxy2, long *sxy3,
			long *p, long *flag);
VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v0, VECTOR *v1);
MATRIX *TransposeMatrix(MATRIX *m0, MATRIX *m1);
void NormalColorCol3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, CVECTOR *v3,
			CVECTOR *v4, CVECTOR *v5, CVECTOR *v6);
long RotNclip3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
			long *sxy0, long *sxy1, long *sxy2, long *p, long *otz,
			long *flag);
long RotNclip4(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2, SVECTOR *v3,
			long *sxy0, long *sxy1, long *sxy2, long *sxy3,
			long *p, long *otz, long *flag);
VECTOR *ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v0, VECTOR *v1);
long RotTransPers(SVECTOR *v0, long *sxy, long *p, long *flag);
long SquareRoot0(long a);
MATRIX *CompMatrix(MATRIX *m0, MATRIX *m1, MATRIX *m2);
MATRIX *MulMatrix(MATRIX *m0, MATRIX *m1);
void NormalColorCol(SVECTOR *v0, CVECTOR *v1, CVECTOR *v2);
void SetLightMatrix(MATRIX *m);
long RotTransPers3(SVECTOR *v0, SVECTOR *v1, SVECTOR *v2,
			long *sxy0, long *sxy1, long *sxy2, long *p, long *flag);

/* Port internals. */
extern int Vcount;
void psyq_raise_irq(int irq); /* runs the InterruptCallback of line irq */
void psyq_libetc_reset(void);
void psyq_libapi_reset(void);
void psyq_libc_reset(void);
void psyq_libcd_reset(void);

#endif
