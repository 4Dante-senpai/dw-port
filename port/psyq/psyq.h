#ifndef PORT_PSYQ_H
#define PORT_PSYQ_H

/*
 * Prototypes of the PsyQ functions the port implements, as in the mw_lib41 headers the game
 * compiles against (external/psyq_headers/mw_lib41/include), plus the port's internal hooks.
 */

/* libetc.h */
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

/* Port internals. */
extern int Vcount;
void psyq_raise_irq(int irq); /* runs the InterruptCallback of line irq */
void psyq_libetc_reset(void);
void psyq_libapi_reset(void);
void psyq_libcd_reset(void);

#endif
