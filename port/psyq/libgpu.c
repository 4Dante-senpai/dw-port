__attribute__((weak)) void port_scene_movie_display(int x, int y, int width, int height);
#include "psyq.h"
#include "clock.h"
#include "ps1mem.h"
#include "../backend/gpu.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* [hypothesis] The game leaves a LoadImage source unchanged until DrawSync.
 * Transfers execute now; the virtual completion event models their device latency.
 * No DMA queue or callback is implemented. Timing constants are calibrated in 3.10. */
static uint64_t busy_until;
static int completion;
static unsigned char debug_level;
static unsigned long *tim_cursor;
static DISPENV last_disp;

static void finish(void *arg) { (void)arg; completion = 0; }
static int idle(void *arg) { (void)arg; return clock_now() >= busy_until; }
static void account(GpuWork work)
{
    uint64_t cost = work.words * CLOCK_GPU_WORD_CYCLES + work.pixels * CLOCK_GPU_PIXEL_CYCLES;
    if (!cost) return;
    clock_cancel(completion);
    if (busy_until < clock_now()) busy_until = clock_now();
    busy_until += cost;
    completion = clock_schedule(busy_until - clock_now(), finish, NULL);
}
static uint32_t xy(int x, int y) { return (uint16_t)x | ((uint32_t)(uint16_t)y << 16); }
static int clamp(int v, int hi) { return v < 0 ? 0 : v > hi ? hi : v; }
static unsigned short clut(int x, int y) { return (uint16_t)(((uint32_t)y << 6) | ((x >> 4) & 63)); }
static void setup(void *p, unsigned char len, unsigned char op)
{
    ((unsigned char *)p)[3] = len;
    ((unsigned char *)p)[7] = op;
}

/* asm:asm/us/main/psyq/libgpu.s ResetGraph / _reset */
int ResetGraph(int mode)
{
    clock_psyq_entry();
    clock_cancel(completion); completion = 0; busy_until = clock_now();
    if ((mode & 7) == 0 || (mode & 7) == 3 || (mode & 7) == 5) {
        debug_level = 0;
        memset(&last_disp, 0xff, sizeof(last_disp));
        ResetCallback();
        if ((mode & 7) == 0) gpu_reset(1);
    }
    return 0;
}
/* asm:asm/us/main/psyq/libgpu.s SetGraphDebug */
int SetGraphDebug(int level)
{
    clock_psyq_entry(); int old = debug_level; debug_level = (unsigned char)level; return old;
}
/* asm:asm/us/main/psyq/libgpu.s SetDispMask */
void SetDispMask(int on) { clock_psyq_entry(); gpu_set_display_enable(on != 0); }
/* asm:asm/us/main/psyq/libgpu.s DrawSync / _sync (no queue) */
int DrawSync(int mode)
{
    clock_psyq_entry();
    if (mode) return !idle(NULL);
    clock_wait_until(idle, NULL);
    return 0;
}
/* asm:asm/us/main/psyq/libgpu.s LoadImage */
int LoadImage(RECT *r, unsigned long *src)
{
    clock_psyq_entry(); account(gpu_load(r->x, r->y, r->w, r->h, (const uint16_t *)src)); return 0;
}
/* asm:asm/us/main/psyq/libgpu.s StoreImage */
int StoreImage(RECT *r, unsigned long *dst)
{
    clock_psyq_entry(); account(gpu_store(r->x, r->y, r->w, r->h, (uint16_t *)dst)); return 0;
}
/* asm:asm/us/main/psyq/libgpu.s ClearImage / _clr */
int ClearImage(RECT *r, unsigned char red, unsigned char green, unsigned char blue)
{
    clock_psyq_entry();
    /* _clr clamps the caller rectangle in place, disables mask checking,
     * and restores the draw area/offset after its internal tile or fill. */
    r->w = (short)clamp(r->w, 1023); r->h = (short)clamp(r->h, 511);
    uint32_t mask = 0xe6000000u;
    gpu_gp0(&mask, 1);
    account(gpu_fill(r->x, r->y, r->w, r->h, red | ((uint32_t)green << 8) | ((uint32_t)blue << 16)));
    return 0;
}
/* asm:asm/us/main/psyq/libgpu.s MoveImage */
int MoveImage(RECT *r, int x, int y)
{
    clock_psyq_entry();
    if (!r->w || !r->h) return -1;
    account(gpu_move(r->x, r->y, x, y, r->w, r->h)); return 0;
}
/* asm:asm/us/main/psyq/libgpu.s LoadClut */
unsigned short LoadClut(unsigned long *src, int x, int y)
{
    clock_psyq_entry(); account(gpu_load(x, y, 256, 1, (const uint16_t *)src)); return clut(x, y);
}
/* asm:asm/us/main/psyq/libgpu.s GetClut */
unsigned short GetClut(int x, int y) { clock_psyq_entry(); return clut(x, y); }
/* asm:asm/us/main/psyq/libgpu.s GetTPage */
unsigned short GetTPage(int tp, int abr, int x, int y)
{
    clock_psyq_entry();
    return ((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) |
           ((x & 0x3ff) >> 6) | ((y & 0x200) << 2);
}
/* asm:asm/us/main/psyq/libgpu.s AddPrim */
void AddPrim(void *ot, void *prim)
{
    clock_psyq_entry();
    uint32_t *o = ot, *p = prim;
    *p = (*p & 0xff000000u) | (*o & PS1_LINK_END);
    *o = (*o & 0xff000000u) | PS1_LINK(prim);
}
/* dw2-pc:psyq/libgpu.c ClearOTagR; terminal no-op queue packets omitted. */
unsigned long *ClearOTagR(unsigned long *ot, int n)
{
    clock_psyq_entry();
    if (n > 0) { ot[0] = PS1_LINK_END; for (int i = 1; i < n; ++i) ot[i] = PS1_LINK(&ot[i-1]); }
    return ot;
}
/* dw2-pc:psyq/libgpu.c DrawOTag; PS1 links and bounded traversal. */
void DrawOTag(unsigned long *ot)
{
    clock_psyq_entry();
    GpuWork total = {0, 0};
    for (unsigned int i = 0; i < 0x100000; ++i) {
        uint32_t tag = *ot;
        GpuWork w = gpu_gp0((const uint32_t *)(ot+1), tag >> 24);
        total.words += w.words; total.pixels += w.pixels;
        if ((tag & PS1_LINK_END) == PS1_LINK_END) { account(total); return; }
        ot = PS1_LINK_PTR(tag & PS1_LINK_END);
    }
    fprintf(stderr, "[libgpu] OT traversal stopped after 0x100000 packets (unterminated list)\n");
    account(total);
}
/* asm:asm/us/main/psyq/libgpu.s SetSemiTrans */
void SetSemiTrans(void *p, int on)
{
    clock_psyq_entry(); unsigned char *code = (unsigned char *)p + 7;
    *code = (*code & ~2u) | (on ? 2 : 0);
}
/* asm:asm/us/main/psyq/libgpu.s SetPolyFT3 */
void SetPolyFT3(POLY_FT3 *p)
{
    clock_psyq_entry(); setup(p, 7, 0x24);
}
/* asm:asm/us/main/psyq/libgpu.s SetPolyF4 */
void SetPolyF4(POLY_F4 *p)
{
    clock_psyq_entry(); setup(p, 5, 0x28);
}
/* asm:asm/us/main/psyq/libgpu.s SetPolyFT4 */
void SetPolyFT4(POLY_FT4 *p)
{
    clock_psyq_entry(); setup(p, 9, 0x2c);
}
/* asm:asm/us/main/psyq/libgpu.s SetPolyG4 */
void SetPolyG4(POLY_G4 *p)
{
    clock_psyq_entry(); setup(p, 8, 0x38);
}
/* asm:asm/us/main/psyq/libgpu.s SetPolyGT4 */
void SetPolyGT4(POLY_GT4 *p)
{
    clock_psyq_entry(); setup(p, 12, 0x3c);
}
/* asm:asm/us/main/psyq/libgpu.s SetLineF2 */
void SetLineF2(LINE_F2 *p)
{
    clock_psyq_entry(); setup(p, 3, 0x40);
}
/* asm:asm/us/main/psyq/libgpu.s SetLineF3 */
void SetLineF3(LINE_F3 *p)
{
    clock_psyq_entry(); setup(p, 5, 0x48); p->pad = 0x55555555;
}
/* asm:asm/us/main/psyq/libgpu.s SetLineF4 */
void SetLineF4(LINE_F4 *p)
{
    clock_psyq_entry(); setup(p, 6, 0x4c); p->pad = 0x55555555;
}
/* asm:asm/us/main/psyq/libgpu.s SetDrawOffset / get_ofs */
void SetDrawOffset(DR_OFFSET *p, unsigned short *ofs)
{
    clock_psyq_entry(); ((unsigned char *)p)[3] = 2;
    p->code[0] = 0xe5000000u | (ofs[0] & 0x7ff) | ((ofs[1] & 0x7ff) << 11);
    p->code[1] = 0;
}
/* asm:asm/us/main/psyq/libgpu.s SetDrawLoad */
void SetDrawLoad(DR_LOAD *p, RECT *r)
{
    clock_psyq_entry();
    int words = (r->w * r->h + 1) / 2;
    unsigned len = (uint32_t)words < 13 ? (unsigned)words + 4 : 0;
    ((unsigned char *)p)[3] = (unsigned char)len;
    p->code[0] = 0xa0000000; p->code[1] = xy(r->x, r->y); p->code[2] = xy(r->w, r->h);
    ((unsigned long *)p)[len] = 0x01000000;
}
/* asm:asm/us/main/psyq/libgpu.s SetDefDispEnv */
DISPENV *SetDefDispEnv(DISPENV *e, int x, int y, int w, int h)
{
    clock_psyq_entry(); memset(e, 0, sizeof(*e)); e->disp = (RECT){x,y,w,h}; return e;
}
/* asm:asm/us/main/psyq/libgpu.s PutDispEnv; TV ranges have no presentation here. */
DISPENV *PutDispEnv(DISPENV *e)
{
    clock_psyq_entry();
    if (memcmp(&e->screen, &last_disp.screen, sizeof(RECT)) ||
        memcmp(&e->disp, &last_disp.disp, sizeof(RECT)) ||
        memcmp(&e->isinter, &last_disp.isinter, 4)) e->pad0 = 0; /* USA NTSC */
    last_disp = *e;
    gpu_set_display(e->disp.x, e->disp.y, e->disp.w, e->disp.h, e->isrgb24);
    if (e->isrgb24 && port_scene_movie_display) port_scene_movie_display(e->disp.x,e->disp.y,e->disp.w,e->disp.h);
    return e;
}
/* asm:asm/us/main/psyq/libgpu.s PutDrawEnv / SetDrawEnv2 / get_tw */
DRAWENV *PutDrawEnv(DRAWENV *e)
{
    clock_psyq_entry(); uint32_t *c = (uint32_t *)e->dr_env.code;
    c[0] = 0xe3000000u | clamp(e->clip.x,1023) | (clamp(e->clip.y,511) << 10);
    c[1] = 0xe4000000u | clamp((int16_t)(e->clip.x+e->clip.w-1),1023) |
           (clamp((int16_t)(e->clip.y+e->clip.h-1),511) << 10);
    c[2] = 0xe5000000u | (e->ofs[0]&2047) | ((e->ofs[1]&2047)<<11);
    c[3] = 0xe1000000u | (e->tpage&0x9ff) | (e->dtd?0x200:0) | (e->dfe?0x400:0);
    c[4] = 0xe2000000u | (((e->tw.x&255)>>3)<<10) | (((e->tw.y&255)>>3)<<15) |
           ((-e->tw.w&255)>>3) | (((-e->tw.h&255)>>3)<<5);
    c[5] = 0xe6000000u;
    int n = 6;
    if (e->isbg) {
        int w=clamp(e->clip.w,1023), h=clamp(e->clip.h,511);
        int tile=(e->clip.x&63)||(w&63);
        c[6]=(tile?0x60000000u:0x02000000u)|e->r0|((uint32_t)e->g0<<8)|((uint32_t)e->b0<<16);
        c[7]=tile?xy(e->clip.x-e->ofs[0],e->clip.y-e->ofs[1]):xy(e->clip.x,e->clip.y);
        c[8]=xy(w,h); n=9;
    }
    e->dr_env.tag = ((uint32_t)n<<24) | PS1_LINK_END;
    account(gpu_gp0(c,n)); return e;
}
/* asm:asm/us/main/psyq/libgpu.s OpenTIM */
int OpenTIM(unsigned long *addr) { clock_psyq_entry(); tim_cursor = addr; return 0; }
/* asm:asm/us/main/psyq/libgpu.s ReadTIM / get_tim_addr */
TIM_IMAGE *ReadTIM(TIM_IMAGE *image)
{
    clock_psyq_entry(); if (!tim_cursor || tim_cursor[0] != 0x10) return NULL;
    unsigned long *block = tim_cursor + 2; image->mode = tim_cursor[1];
    if (image->mode & 8) {
        image->crect = (RECT *)(block+1); image->caddr = block+3; block += block[0] >> 2;
    } else { image->crect = NULL; image->caddr = NULL; }
    image->prect = (RECT *)(block+1); image->paddr = block+3;
    tim_cursor = block + (block[0] >> 2); return image;
}
