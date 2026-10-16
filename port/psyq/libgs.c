/* Adapted from DW2-PC (CC0): psyq/libgs.c @ 816ed0d */
/* asm:asm/us/main/psyq/libgs.s (mw_lib41).
 * DW1's view normalization and packet formats follow DW1 asm, not Psy-Q 4.7. */
#include "clock.h"
#include "psyq.h"
#include "ps1mem.h"
#include "../backend/gte.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

PACKET *GsOUT_PACKET_P;
MATRIX GsIDMATRIX = {{{4096,0,0},{0,4096,0},{0,0,4096}}, {0,0,0}};
MATRIX GsWSMATRIX, GsLIGHTWSMATRIX;
_GsFCALL GsFCALL4;
static MATRIX GsIDMATRIX2, LC;
/* Written by the retail library; readers are outside the game-used subset. */
static MATRIX GsWSMATRIX_ORG __attribute__((unused));
static DRAWENV draw;
static DISPENV disp;
static unsigned long clear[2][4], frame = 1;
static short origin[2], orgofs[2], bufx[2], bufy[2];
static int width, height, active, gpu_offsets, light_mode;
static int near_clip __attribute__((unused)), far_clip __attribute__((unused));
static unsigned ton;

static uint32_t word(const void *p) { uint32_t v; memcpy(&v,p,4); return v; }
static uint16_t half(const void *p) { uint16_t v; memcpy(&v,p,2); return v; }
static void put(void *p, uint32_t v) { memcpy(p,&v,4); }
static uint32_t xy(int x,int y) { return (uint16_t)x | (uint32_t)(uint16_t)y << 16; }
static void matrix_load(const MATRIX *m,int reg)
{ for(int i=0;i<5;i++) gte_ctc2(reg+i,word((const char *)m+i*4)); }
static void ls_matrix(MATRIX *m)
{
    matrix_load(m,GTE_RT0);
    for(int i=0;i<3;i++) gte_ctc2(GTE_TRX+i,(uint32_t)m->t[i]);
}
/* asm:asm/us/main/psyq/libgs.s _make_packet */
static PACKET *_make_packet(PACKET *p,GsOT *ot,unsigned short pri,unsigned len)
{
    long index=(long)pri-ot->offset;
    PORT_ASSERT(index>=0);
    unsigned long *tag=(unsigned long *)ot->org+index;
    put(p,(*tag & 0xffffffu) | len<<24);
    *tag=PS1_LINK(p);
    return p+4*(len+1);
}
/* asm:asm/us/main/psyq/libgs.s GsSetDrawBuffOffset */
static void GsSetDrawBuffOffset(void)
{
    if(gpu_offsets) {
        orgofs[0]=orgofs[1]=0;
        draw.ofs[0]=origin[0]+bufx[active]; draw.ofs[1]=origin[1]+bufy[active];
        PutDrawEnv(&draw);
    } else {
        int x=origin[0]+bufx[!active], y=origin[1]+bufy[!active];
        gte_ctc2(GTE_OFX,(uint32_t)x<<16); gte_ctc2(GTE_OFY,(uint32_t)y<<16);
        orgofs[0]=x;orgofs[1]=y;
    }
}
/* asm:asm/us/main/psyq/libgs.s GsSetDrawBuffClip */
static void GsSetDrawBuffClip(void)
{
    draw.clip=(RECT){bufx[active],bufy[active],width,height}; PutDrawEnv(&draw);
}
/* asm:asm/us/main/psyq/libgs.s gpu_init */
static void gpu_init(unsigned short w,unsigned short h,unsigned short mode,unsigned short dith,unsigned short vram)
{
    ResetGraph(((mode>>4)&3)==3?3:0);
    memset(&draw,0,sizeof(draw)); draw.dtd=dith; PutDrawEnv(&draw);
    memset(&disp,0,sizeof(disp)); disp.disp.w=w;disp.disp.h=h;
    disp.isinter=mode&1;disp.isrgb24=vram;gpu_offsets=mode&4;PutDispEnv(&disp);
}
/* asm:asm/us/main/psyq/libgs.s gte_init */
static void gte_init(void)
{
    /* InitGeom constants, asm libgte.s; no backend reset of unrelated registers. */
    gte_ctc2(GTE_ZSF3,0x155);gte_ctc2(GTE_ZSF4,0x100);
    gte_ctc2(GTE_H,1000);gte_ctc2(GTE_DQA,0xffffef9e);gte_ctc2(GTE_DQB,0x1400000);
    gte_ctc2(GTE_RFC,0);gte_ctc2(GTE_GFC,0);gte_ctc2(GTE_BFC,0);
    gte_ctc2(GTE_OFX,0);gte_ctc2(GTE_OFY,0);orgofs[0]=orgofs[1]=0;
}
/* asm:asm/us/main/psyq/libgs.s valiable_init */
static void valiable_init(unsigned short w,unsigned short h)
{
    PORT_ASSERT(w); width=w;height=h;
    memset(&GsIDMATRIX,0,sizeof(GsIDMATRIX));
    for(int i=0;i<3;i++) GsIDMATRIX.m[i][i]=4096;
    GsIDMATRIX2=GsIDMATRIX;GsIDMATRIX2.m[1][1]=((h<<14)/w)/3;
    memset(&GsLIGHTWSMATRIX,0,sizeof(MATRIX)); LC=GsLIGHTWSMATRIX;
    memset(bufx,0,sizeof(bufx));memset(bufy,0,sizeof(bufy));origin[0]=origin[1]=0;
    memset(clear,0,sizeof(clear));clear[0][0]=clear[1][0]=0x03000000;
    clear[0][1]=clear[1][1]=0x02000000;frame=1;
}
/* asm:asm/us/main/psyq/libgs.s GsInitGraph */
void GsInitGraph(unsigned short w,unsigned short h,unsigned short mode,unsigned short dith,unsigned short vram)
{ clock_psyq_entry();gpu_init(w,h,mode,dith,vram);gte_init();active=0;valiable_init(w,h);GsSetDrawBuffClip();GsSetDrawBuffOffset(); }
/* asm:asm/us/main/psyq/libgs.s GsInit3D */
void GsInit3D(void)
{ clock_psyq_entry();origin[0]=width/2;origin[1]=height/2;GsSetDrawBuffOffset();near_clip=10;far_clip=0x3fff;light_mode=0; }
/* asm:asm/us/main/psyq/libgs.s GsSetWorkBase */
void GsSetWorkBase(PACKET *p) { clock_psyq_entry();GsOUT_PACKET_P=p; }
/* asm:asm/us/main/psyq/libgs.s GsGetWorkBase */
PACKET *GsGetWorkBase(void) { clock_psyq_entry();return GsOUT_PACKET_P; }
/* asm:asm/us/main/psyq/libgs.s GsGetActiveBuff */
int GsGetActiveBuff(void) { clock_psyq_entry();return active; }
/* asm:asm/us/main/psyq/libgs.s GsSetOrign */
void GsSetOrign(long x,long y) { clock_psyq_entry();origin[0]=x;origin[1]=y; }
/* asm:asm/us/main/psyq/libgs.s GsSetNearClip */
void GsSetNearClip(long z) { clock_psyq_entry();near_clip=z; }
/* asm:asm/us/main/psyq/libgs.s GsDefDispBuff */
void GsDefDispBuff(unsigned short x0,unsigned short y0,unsigned short x1,unsigned short y1)
{ clock_psyq_entry();bufx[0]=x0;bufx[1]=x1;bufy[0]=y0;bufy[1]=y1;GsSetDrawBuffClip();GsSetDrawBuffOffset(); }
/* asm:asm/us/main/psyq/libgs.s GsSwapDispBuff */
void GsSwapDispBuff(void)
{
    clock_psyq_entry();disp.disp.x=bufx[active];disp.disp.y=bufy[active];PutDispEnv(&disp);SetDispMask(1);
    if(++frame==0) frame=1;
    active=!active;GsSetDrawBuffClip();GsSetDrawBuffOffset();
}
/* asm:asm/us/main/psyq/libgs.s GsSortClear */
void GsSortClear(unsigned char r,unsigned char g,unsigned char b,GsOT *ot)
{
    clock_psyq_entry();unsigned long *p=clear[active];
    p[1]=0x02000000u|r|(unsigned)g<<8|(unsigned)b<<16;
    p[2]=xy(bufx[active],bufy[active]);p[3]=xy(disp.isrgb24?width*3/2:width,height);AddPrim(ot->tag,p);
}
/* asm:asm/us/main/psyq/libgs.s GsDrawOt */
void GsDrawOt(GsOT *ot) { clock_psyq_entry();DrawOTag((unsigned long *)ot->tag); }
/* asm:asm/us/main/psyq/libgs.s GsClearOt */
void GsClearOt(unsigned short offset,unsigned short point,GsOT *ot)
{
    clock_psyq_entry();ot->offset=offset;ot->point=point;unsigned n=1u<<(ot->length&31);
    ot->tag=ot->org+n-1;ClearOTagR((unsigned long *)ot->org,n);
}
/* asm:asm/us/main/psyq/libgs.s GsSortOt */
GsOT *GsSortOt(GsOT *src,GsOT *dst)
{
    clock_psyq_entry();unsigned long *node=(unsigned long *)src->org,*previous=node,*next=node;
    while((*node&0xffffffu)!=PS1_LINK_END) {
        previous=next;next=node;node=PS1_LINK_PTR(*node&0xffffffu);
    }
    unsigned long *target=(unsigned long *)dst->org+(src->point-dst->offset);
    *previous=(*previous&0xff000000u)|(*target&0xffffffu);
    *target=(*target&0xff000000u)|PS1_LINK(src->tag);return dst;
}
/* asm:asm/us/main/psyq/libgs.s GsInitCoordinate2 */
void GsInitCoordinate2(GsCOORDINATE2 *super,GsCOORDINATE2 *base)
{ clock_psyq_entry();base->coord=GsIDMATRIX;base->super=super;base->flg=0;if((uintptr_t)super>=2) super->sub=base; }
/* asm:asm/us/main/psyq/libgs.s GsSetLsMatrix */
void GsSetLsMatrix(MATRIX *m) { clock_psyq_entry();ls_matrix(m); }
/* asm:asm/us/main/psyq/libgs.s GsSetLightMatrix */
void GsSetLightMatrix(MATRIX *m)
{ clock_psyq_entry();MATRIX out=GsLIGHTWSMATRIX;PushMatrix();MulMatrix(&out,m);PopMatrix();matrix_load(&out,GTE_LLM0); }
/* asm:asm/us/main/psyq/libgs.s GsMulCoord0 */
void GsMulCoord0(MATRIX *a,MATRIX *b,MATRIX *out)
{
    clock_psyq_entry();ApplyMatrixLV(a,(VECTOR *)b->t,(VECTOR *)out->t);MulMatrix0(a,b,out);
    for(int i=0;i<3;i++) out->t[i]=(uint32_t)out->t[i]+(uint32_t)a->t[i];
}
/* asm:asm/us/main/psyq/libgs.s GsMulCoord2 */
static void GsMulCoord2(MATRIX *a,MATRIX *b)
{
    VECTOR t;ApplyMatrixLV(a,(VECTOR *)b->t,&t);MulMatrix0(a,b,b);
    for(int i=0;i<3;i++) b->t[i]=((uint32_t *)&t)[i]+(uint32_t)a->t[i];
}
/* asm:asm/us/main/psyq/libgs.s GsMulCoord3 */
void GsMulCoord3(MATRIX *a,MATRIX *b)
{
    clock_psyq_entry();VECTOR t;ApplyMatrixLV(a,(VECTOR *)b->t,&t);MulMatrix(a,b);
    for(int i=0;i<3;i++) a->t[i]=((uint32_t *)&t)[i]+(uint32_t)a->t[i];
}
/* asm:asm/us/main/psyq/libgs.s GsGetLw, GsGetLs, GsGetLws shared coordinate cache. */
static void get_lw(GsCOORDINATE2 *coord,MATRIX *m)
{
    GsCOORDINATE2 *stack[101];int i=0,dirty=100;
    for(;;) {
        PORT_ASSERT(i<101);stack[i]=coord;
        if(coord->super==NULL) {
            if(coord->flg==frame||coord->flg==0) {coord->workm=coord->coord;*m=coord->workm;coord->flg=frame;}
            else if(dirty==100) {*m=stack[0]->workm;i=0;}
            else {i=dirty+1;*m=stack[i]->workm;}
            break;
        }
        if(coord->flg==frame) {*m=coord->workm;break;}
        if(coord->flg==0) dirty=i;
        coord=coord->super;i++;
    }
    for(;i>0;i--) {GsCOORDINATE2 *c=stack[i-1];GsMulCoord3(m,&c->coord);c->workm=*m;c->flg=frame;}
}
/* asm:asm/us/main/psyq/libgs.s GsGetLw */
void GsGetLw(GsCOORDINATE2 *c,MATRIX *m) {clock_psyq_entry();get_lw(c,m);}
/* asm:asm/us/main/psyq/libgs.s GsGetLs */
void GsGetLs(GsCOORDINATE2 *c,MATRIX *m) {clock_psyq_entry();get_lw(c,m);GsMulCoord2(&GsWSMATRIX,m);}
/* asm:asm/us/main/psyq/libgs.s GsGetLws */
void GsGetLws(GsCOORDINATE2 *c,MATRIX *w,MATRIX *s) {clock_psyq_entry();get_lw(c,w);*s=*w;GsMulCoord2(&GsWSMATRIX,s);}
/* asm:asm/us/main/psyq/libgs.s GsSetProjection */
void GsSetProjection(long h) {clock_psyq_entry();gte_ctc2(GTE_H,h);}
/* asm:asm/us/main/psyq/libgs.s gte_set_lc, gte_read_lc */
static void gte_set_lc(MATRIX *m) {LC=*m;matrix_load(m,GTE_LCM0);}
static void gte_read_lc(MATRIX *m) {*m=LC;}
/* asm:asm/us/main/psyq/libgs.s GsSetFlatLight */
int GsSetFlatLight(int id,GsF_LIGHT *lt)
{
    clock_psyq_entry();MATRIX l=GsLIGHTWSMATRIX,c;gte_read_lc(&c);
    uint32_t x=lt->vx,y=lt->vy,z=lt->vz;long len=SquareRoot0(x*x+y*y+z*z);
    if(!len) return -1;
    if(id>=0&&id<3) {
        l.m[id][0]=(int32_t)((0u-x)<<12)/len;l.m[id][1]=(int32_t)((0u-y)<<12)/len;l.m[id][2]=(int32_t)((0u-z)<<12)/len;
        c.m[0][id]=(lt->r<<12)/255;c.m[1][id]=(lt->g<<12)/255;c.m[2][id]=(lt->b<<12)/255;
    }
    GsLIGHTWSMATRIX=l;gte_set_lc(&c);return 0;
}
/* asm:asm/us/main/psyq/libgs.s GsSetLightMode */
void GsSetLightMode(int mode) {clock_psyq_entry();if(mode>=0&&mode<=3) light_mode=mode;else printf("GsSetLightMode: unknown mode %d\n",mode);}
/* asm:asm/us/main/psyq/libgs.s GsSetAmbient */
void GsSetAmbient(long r,long g,long b)
{clock_psyq_entry();gte_ctc2(GTE_RBK,(uint32_t)(r>>4)<<4);gte_ctc2(GTE_GBK,(uint32_t)(g>>4)<<4);gte_ctc2(GTE_BBK,(uint32_t)(b>>4)<<4);}
/* asm:asm/us/main/psyq/libgs.s GsGetTimInfo (input points at TIM flags, past magic). */
void GsGetTimInfo(unsigned long *im,GsIMAGE *t)
{
    clock_psyq_entry();t->pmode=*im++;if(t->pmode&8) {
        unsigned long *next=im+(*im/4);t->cx=half(im+1);t->cy=half((char *)(im+1)+2);
        t->cw=half(im+2);t->ch=half((char *)(im+2)+2);t->clut=im+3;im=next;
    }
    t->px=half(im+1);t->py=half((char *)(im+1)+2);t->pw=half(im+2);t->ph=half((char *)(im+2)+2);t->pixel=im+3;
}
/* asm:asm/us/main/psyq/libgs.s Gssub_make_matrix */
static void Gssub_make_matrix(MATRIX *m,short s,short c,int axis)
{
    *m=GsIDMATRIX;
    if(axis=='x') {m->m[1][1]=c;m->m[2][2]=c;m->m[1][2]=-s;m->m[2][1]=s;}
    else if(axis=='y') {m->m[0][0]=c;m->m[2][2]=c;m->m[0][2]=s;m->m[2][0]=-s;}
    else {m->m[0][0]=c;m->m[1][1]=c;m->m[0][1]=-s;m->m[1][0]=s;}
}
/* asm:asm/us/main/psyq/libgs.s gte_rotate_z_matrix */
static void gte_rotate_z_matrix(MATRIX *m,int angle)
{int c=rcos(angle/360),s=rsin(angle/360);if(angle) {MATRIX r;Gssub_make_matrix(&r,s,c,'z');MulMatrix(m,&r);}}
/* asm:asm/us/main/psyq/libgs.s len_param, scale_view_param */
static int len_param(int x) {int n=0;while(x>0) {x>>=1;n++;}return n;}
static void scale_view_param(GsRVIEW2 *v,long p[6])
{
    long input[]={v->vpx,v->vpy,v->vpz,v->vrx,v->vry,v->vrz};int max=0;
    for(int i=0;i<6;i++) {int a=input[i]<0?(int)(0u-(uint32_t)input[i]):input[i];if(a>max) max=a;}
    int shift=len_param(max)-15;if(shift<0) shift=0;
    for(int i=0;i<6;i++) p[i]=input[i]>>shift;
}
static void view_super(GsCOORDINATE2 *super)
{
    if(super) {
        MATRIX lw,tr;VECTOR t;get_lw(super,&lw);TransposeMatrix(&lw,&tr);ApplyMatrixLV(&tr,(VECTOR *)lw.t,&t);
        tr.t[0]=0u-(uint32_t)t.vx;tr.t[1]=0u-(uint32_t)t.vy;tr.t[2]=0u-(uint32_t)t.vz;
        GsMulCoord2(&GsWSMATRIX,&tr);GsWSMATRIX=tr;
    }
    GsWSMATRIX_ORG=GsWSMATRIX;
}
/* asm:asm/us/main/psyq/libgs.s GsSetRefView2 */
int GsSetRefView2(GsRVIEW2 *v)
{
    clock_psyq_entry();GsWSMATRIX=GsIDMATRIX2;gte_rotate_z_matrix(&GsWSMATRIX,0u-(uint32_t)v->rz);
    long p[6];scale_view_param(v,p);uint32_t dx=(uint32_t)p[3]-p[0],dy=(uint32_t)p[4]-p[1],dz=(uint32_t)p[5]-p[2];
    long len=SquareRoot0(dx*dx+dy*dy+dz*dz);if(!len) return 1;
    long horiz=SquareRoot0(dx*dx+dz*dz);MATRIX r;
    Gssub_make_matrix(&r,(int32_t)(dy<<12)/len,(int32_t)((uint32_t)horiz<<12)/len,'x');MulMatrix(&GsWSMATRIX,&r);
    if(horiz) {Gssub_make_matrix(&r,0u-(uint32_t)((int32_t)(dx<<12)/horiz),(int32_t)(dz<<12)/horiz,'y');MulMatrix(&GsWSMATRIX,&r);}
    VECTOR eye={0u-(uint32_t)v->vpx,0u-(uint32_t)v->vpy,0u-(uint32_t)v->vpz,0};
    ApplyMatrixLV(&GsWSMATRIX,&eye,(VECTOR *)GsWSMATRIX.t);view_super(v->super);return 0;
}
/* asm:asm/us/main/psyq/libgs.s GsSetView2 */
int GsSetView2(GsVIEW2 *v) {clock_psyq_entry();GsWSMATRIX=v->view;view_super(v->super);return 0;}
/* asm:asm/us/main/psyq/libgs.s GsSortBoxFill */
void GsSortBoxFill(GsBOXF *b,GsOT *ot,unsigned short pri)
{
    clock_psyq_entry();uint32_t a=b->attribute;if(a>>31) return;PACKET *p=GsOUT_PACKET_P;
    put(p+4,0xe1000200u|((a>>17)&0x180)|((a>>23)&0x60));
    put(p+8,b->r|(unsigned)b->g<<8|(unsigned)b->b<<16|(0x60u|((a>>29)&2))<<24);
    put(p+12,xy(b->x+orgofs[0],b->y+orgofs[1]));put(p+16,xy(b->w,b->h));
    GsOUT_PACKET_P=_make_packet(p,ot,pri,4);
}
static void sprite_packet(GsSPRITE *s,GsOT *ot,unsigned short pri,int pivot)
{
    uint32_t a=s->attribute;PACKET *p=GsOUT_PACKET_P;
    put(p+4,0xe1000200u|(s->tpage&31)|((a>>17)&0x180)|((a>>23)&0x60));
    put(p+8,0x64000000u|((a>>5)&0x02000000u)|((a<<18)&0x01000000u)|s->r|(unsigned)s->g<<8|(unsigned)s->b<<16);
    put(p+12,xy(s->x+orgofs[0]-(pivot?s->mx:0),s->y+orgofs[1]-(pivot?s->my:0)));
    put(p+16,s->u|(unsigned)s->v<<8|((uint32_t)(unsigned short)s->cy<<22)|(((uint32_t)(unsigned short)s->cx<<12)&0x3f0000));
    put(p+20,xy(s->w,s->h));GsOUT_PACKET_P=_make_packet(p,ot,pri,5);
}
/* asm:asm/us/main/psyq/libgs.s GsSortFastSprite */
void GsSortFastSprite(GsSPRITE *s,GsOT *ot,unsigned short pri)
{clock_psyq_entry();if(!(s->attribute>>31)&&s->w&&s->h) sprite_packet(s,ot,pri,0);}
/* asm:asm/us/main/psyq/libgs.s GsSortSprite */
void GsSortSprite(GsSPRITE *s,GsOT *ot,unsigned short pri)
{
    clock_psyq_entry();uint32_t a=s->attribute;if((a>>31)||!s->w||!s->h) return;
    if((a&(1u<<27))||(s->scalex==4096&&s->scaley==4096&&!s->rotate&&!(a&0xc00000))) {sprite_packet(s,ot,pri,1);return;}
    MATRIX m=GsIDMATRIX;
    if(s->rotate) {SVECTOR r={0,0,s->rotate/360,0};RotMatrix(&r,&m);}
    if(s->scalex!=4096||s->scaley!=4096) {VECTOR scale={s->scalex,s->scaley,0,0};ScaleMatrix(&m,&scale);}
    m.t[0]=s->x;m.t[1]=s->y;m.t[2]=gte_cfc2(GTE_H);ls_matrix(&m);
    SVECTOR v[4];long screen[4];
    for(int i=0;i<4;i++) {v[i]=(SVECTOR){(short)(-s->mx+((i&1)?s->w:0)),(short)(-s->my+((i&2)?s->h:0)),0,0};}
    port_gte_ldv3(v,v+1,v+2);gte_command(GTE_CMD_RTPT);
    for(int i=0;i<3;i++) screen[i]=gte_mfc2(GTE_SXY0+i);
    port_gte_ldv0(v+3);gte_command(GTE_CMD_RTPS);screen[3]=gte_mfc2(GTE_SXY2);
    int u0=s->u,u1=s->u+(unsigned char)s->w-1,v0=s->v,v1=s->v+(unsigned char)s->h-1;
    if(a&0x800000) {int t=u0;u0=u1;u1=t;}if(a&0x400000) {int t=v0;v0=v1;v1=t;}
    PACKET *p=GsOUT_PACKET_P;
    put(p+4,0x2c000000u|((a>>5)&0x2000000)|((a<<18)&0x1000000)|s->r|(unsigned)s->g<<8|(unsigned)s->b<<16);
    for(int i=0;i<4;i++) put(p+8+8*i,screen[i]);
    put(p+12,(u0&255)|((v0&255)<<8)|((uint32_t)(unsigned short)s->cy<<22)|(((uint32_t)(unsigned short)s->cx<<12)&0x3f0000));
    put(p+20,(u1&255)|((v0&255)<<8)|((s->tpage&31)<<16)|((a>>1)&0x1800000)|((a>>7)&0x600000));
    put(p+28,(u0&255)|((v1&255)<<8));put(p+36,(u1&255)|((v1&255)<<8));
    GsOUT_PACKET_P=_make_packet(p,ot,pri,9);
}
/* asm:asm/us/main/psyq/libgs.s GsSortPoly */
void GsSortPoly(void *pp,GsOT *ot,unsigned short pri)
{
    clock_psyq_entry();const unsigned char *s=pp;PACKET *p=GsOUT_PACKET_P;unsigned code=s[7],len=0;
    put(p+4,word(s+4));len++;unsigned off=8;
    for(unsigned i=0;i<((code&8)?4u:3u);i++) {
        if(i&&(code&16)) {put(p+4+4*len,word(s+off));off+=4;len++;}
        put(p+4+4*len,xy((short)half(s+off)+orgofs[0],(short)half(s+off+2)+orgofs[1]));off+=4;len++;
        if(code&4) {put(p+4+4*len,word(s+off));off+=4;len++;}
    }
    GsOUT_PACKET_P=_make_packet(p,ot,pri,len);
}
/* asm:asm/us/main/psyq/libgs.s GsMapModelingData.
 * [hypothesis] Disk TMDs arrive unmapped; the mapped bit prevents applying relocation twice.
 * Stored addresses are 32-bit port pointers, not PS1 addresses or OT links. */
void GsMapModelingData(unsigned long *p)
{
    clock_psyq_entry();if(*p&1) return;*p|=1;long count=p[1];unsigned long *base=p+2;
    for(long i=0;i<count;i++) {unsigned long *o=base+i*7;o[0]+=(uintptr_t)base;o[2]+=(uintptr_t)base;o[4]+=(uintptr_t)base;}
}
/* DW1's primitive headers become {u16 run_count,u8 flags,u8 mode} in GsLinkObject4. */
static unsigned tmd_size(unsigned mode,unsigned flags)
{
    int n=(mode&8)?4:3,tex=mode&4,gouraud=mode&16,unlit=mode&1;
    if(tex) return 4+4*n+(unlit?4*(gouraud?n:1):0)+(unlit?((2*n+3)&~3):(gouraud?4*n:((2*(n+1)+3)&~3)));
    return 4+4*((unlit&&gouraud)?n:((flags&4)?n:1))+(unlit?((2*n+3)&~3):(gouraud?4*n:((2*(n+1)+3)&~3)));
}
/* asm:asm/us/main/psyq/libgs.s GsLinkObject4 */
void GsLinkObject4(unsigned long base,GsDOBJ2 *obj,int n)
{
    clock_psyq_entry();unsigned long *o=(unsigned long *)(uintptr_t)base+7*n;obj->tmd=o;
    unsigned char *p=(void *)(uintptr_t)o[4],*start=p;unsigned count=0,last=0;
    for(unsigned i=0;i<o[5];i++) {
        unsigned key=half(p+2);
        if(i&&key!=last) {unsigned short c=count;memcpy(start,&c,2);start=p;count=0;}
        last=key;count++;p+=tmd_size(p[3],p[2]);
    }
    if(o[5]) {unsigned short c=count;memcpy(start,&c,2);}
}
/* TMD kernels share the object transparency bit, which the asm stores in GsTON. */
/* asm:asm/us/main/psyq/libgte.s GsTMDfast*, GsTMDdiv*, RCpoly* and Add_Prim_*.
 * This file is included privately by libgs.c so object state stays static.
 * Kernels never advance the PsyQ clock: only GsSortObject4J is a game entry. */
typedef struct {
    SVECTOR v;
    uint32_t uv, color, screen, z;
} TmdVertex;
_Static_assert(sizeof(TmdVertex)==24,"RCpoly vertex layout");

static void tmd_project3(TmdVertex *v)
{
    port_gte_ldv3(&v[0].v,&v[1].v,&v[2].v);gte_command(GTE_CMD_RTPT);
    for(int i=0;i<3;i++) {v[i].screen=gte_mfc2(GTE_SXY0+i);v[i].z=gte_mfc2(GTE_SZ1+i);}
}
static void tmd_project1(TmdVertex *v)
{port_gte_ldv0(&v->v);gte_command(GTE_CMD_RTPS);v->screen=gte_mfc2(GTE_SXY2);v->z=gte_mfc2(GTE_SZ3);}
static PACKET *tmd_emit(PACKET *pk,const TmdVertex *v,int count,int texture,int gouraud,
                        unsigned long *tag,uint32_t color)
{
    unsigned len=(gouraud?2*count:count+1)+(texture?count:0);
    put(pk,(*tag&0xffffffu)|(len<<24));*tag=(*tag&0xff000000u)|PS1_LINK(pk);
    unsigned off=4;
    for(int i=0;i<count;i++) {
        if(!i||gouraud) {put(pk+off,i?v[i].color:color);off+=4;}
        put(pk+off,v[i].screen);off+=4;
        if(texture) {put(pk+off,v[i].uv);off+=4;}
    }
    return pk+off;
}
/* Decode the fixed DW1 TMD formats. mode: 0 lit, 1 no normals, 2 lighting off. */
static void tmd_decode(const unsigned char *p,SVECTOR *verts,SVECTOR *norms,
                       int count,int texture,int gouraud,int mode,TmdVertex v[4],unsigned normals[4])
{
    unsigned index=texture?4+4*count:8;
    if(mode==1) {
        index=(texture?4+4*count:4)+4*(gouraud?count:1);
        for(int i=0;i<count;i++) {
            v[i].v=verts[half(p+index+2*i)];normals[i]=0;
            v[i].color=word(p+(texture?4+4*count:4)+4*(gouraud?i:0));
        }
    } else {
        unsigned normal=index;
        for(int i=0;i<count;i++) {
            unsigned vi=gouraud?index+2+4*i:index+2+2*i;
            normals[i]=half(p+normal+(gouraud?4*i:0));v[i].v=verts[half(p+vi)];
            v[i].color=texture?0x808080:word(p+4);
        }
    }
    for(int i=0;i<count;i++) v[i].uv=texture?word(p+4+4*i):0;
    (void)norms;
}
static uint32_t tmd_color(const unsigned char *p,TmdVertex v[4],SVECTOR *norms,unsigned normals[4],
                          int count,int texture,int gouraud,int mode)
{
    uint32_t command=texture?((p[3]|ton*2u)&(mode==1?0xfeu:0xffu))<<24:word(p+4)|(ton<<25);
    uint32_t base=texture?0x808080:word(p+4)&0xffffffu;
    if(mode==0) {
        gte_mtc2(GTE_RGBC,base|(command&0xff000000u));
        if(gouraud) {
            if(count==4) {port_gte_ldv0(norms+normals[3]);gte_command(GTE_CMD_NCCS);v[3].color=gte_mfc2(GTE_RGB2);}
            port_gte_ldv3(norms+normals[0],norms+normals[1],norms+normals[2]);gte_command(GTE_CMD_NCCT);
            for(int i=0;i<3;i++) v[i].color=gte_mfc2(GTE_RGB0+i);
        } else {port_gte_ldv0(norms+normals[0]);gte_command(GTE_CMD_NCCS);v[0].color=gte_mfc2(GTE_RGB2);}
    } else if(mode==2) {
        /* The fast TG*NL kernels emit a flat, raw-texture polygon. */
        if(texture&&gouraud) command=((count==4?0x2du:0x25u)|(p[3]&2)|ton*2u)<<24;
        v[0].color=base|command;
    } else v[0].color=(v[0].color&0xffffffu)|(command&0xff000000u);
    return v[0].color;
}
static PACKET *tmd_fast(void *prim,SVECTOR *verts,SVECTOR *norms,PACKET *pk,unsigned n,int shift,
                        GsOT *ot,int count,int texture,int gouraud,int mode)
{
    if(!n) return pk;
    const unsigned char *p=prim;unsigned size=tmd_size(p[3],p[2]);
    for(unsigned k=0;k<n;k++,p+=size) {
        TmdVertex v[4]={0};unsigned normals[4];tmd_decode(p,verts,norms,count,texture,gouraud,mode,v,normals);
        tmd_project3(v);if(gte_cfc2(GTE_FLAG)&0x80000000u) continue;
        gte_command(GTE_CMD_NCLIP);if((int32_t)gte_mfc2(GTE_MAC0)<=0) continue;
        if(count==4) {tmd_project1(v+3);if(gte_cfc2(GTE_FLAG)&0x80000000u) continue;}
        gte_command(count==4?GTE_CMD_AVSZ4:GTE_CMD_AVSZ3);unsigned z=gte_mfc2(GTE_OTZ);
        uint32_t color=tmd_color(p,v,norms,normals,count,texture,gouraud,mode);
        unsigned index=((z-ot->offset)>>(shift&31))&0xffff;
        pk=tmd_emit(pk,v,count,texture,gouraud&&mode!=2,(unsigned long *)ot->org+index,color);
    }
    return pk;
}
/* RCpoly subdivision culls against H/2 and the screen rectangle, then bisects in
 * model space. Every child shares the original primitive's OT bucket. */
static int tmd_outside(const TmdVertex *v,int n,unsigned w,unsigned h)
{
    int cx=(int32_t)gte_cfc2(GTE_OFX)>>16,cy=(int32_t)gte_cfc2(GTE_OFY)>>16;
    unsigned near=(int32_t)gte_cfc2(GTE_H)>>1;int allnear=1,left=1,right=1,top=1,bottom=1;
    for(int i=0;i<n;i++) {
        int x=(short)v[i].screen,y=(short)(v[i].screen>>16);
        allnear &= v[i].z<near;left &= x<cx-(int)(w>>1);right &= x>cx+(int)(w>>1);
        top &= y<cy-(int)(h>>1);bottom &= y>cy+(int)(h>>1);
    }
    return allnear||left||right||top||bottom;
}
static TmdVertex tmd_mid(const TmdVertex *a,const TmdVertex *b)
{
    TmdVertex v={0};v.v.vx=(a->v.vx+b->v.vx)>>1;v.v.vy=(a->v.vy+b->v.vy)>>1;v.v.vz=(a->v.vz+b->v.vz)>>1;
    for(int i=0;i<2;i++) v.uv|=((((a->uv>>(8*i))&255)+((b->uv>>(8*i))&255))>>1)<<(8*i);
    for(int i=0;i<3;i++) v.color|=((((a->color>>(8*i))&255)+((b->color>>(8*i))&255))>>1)<<(8*i);
    return v;
}
static PACKET *tmd_subdivide(PACKET *pk,TmdVertex v[4],unsigned depth,unsigned limit,int count,int gouraud,
                             unsigned long *tag,uint32_t color,unsigned clut,unsigned tpage,unsigned w,unsigned h)
{
    if(tmd_outside(v,count,w,h)) return pk;
    TmdVertex m[5]={0};
    if(count==3) {
        m[0]=tmd_mid(v,v+1);m[1]=tmd_mid(v+1,v+2);m[2]=tmd_mid(v+2,v);tmd_project3(m);
    } else {
        m[0]=tmd_mid(v,v+1);m[1]=tmd_mid(v,v+2);m[2]=tmd_mid(v+1,v+3);m[3]=tmd_mid(v+2,v+3);
        /* The centre uses a single four-way average, not two rounded midpoints. */
        for(int axis=0;axis<3;axis++) {
            int sum=0;for(int i=0;i<4;i++) sum+=((short *)&v[i].v)[axis];((short *)&m[4].v)[axis]=sum>>2;
        }
        for(int j=0;j<3;j++) {unsigned sum=0;for(int i=0;i<4;i++) sum+=(v[i].color>>(8*j))&255;m[4].color|=(sum>>2)<<(8*j);}
        for(int j=0;j<2;j++) {unsigned sum=0;for(int i=0;i<4;i++) sum+=(v[i].uv>>(8*j))&255;m[4].uv|=(sum>>2)<<(8*j);}
        TmdVertex batch[3]={m[0],m[1],m[4]};tmd_project3(batch);m[0]=batch[0];m[1]=batch[1];m[4]=batch[2];
        batch[0]=m[2];batch[1]=m[3];tmd_project3(batch);m[2]=batch[0];m[3]=batch[1];
    }
    TmdVertex child[4][4];
    if(count==3) {
        TmdVertex c[4][4]={{v[0],m[0],m[2]},{v[1],m[1],m[0]},{v[2],m[2],m[1]},{m[0],m[1],m[2]}};
        memcpy(child,c,sizeof(c));
    } else {
        TmdVertex c[4][4]={{v[0],m[0],m[1],m[4]},{v[1],m[2],m[0],m[4]},
                         {v[2],m[1],m[3],m[4]},{v[3],m[3],m[2],m[4]}};memcpy(child,c,sizeof(c));
    }
    if(depth+1==limit) {
        const int tri_order[]={1,3,0,2};
        for(int i=0;i<4;i++) {
            TmdVertex *c=child[count==3?tri_order[i]:i];
            c[0].uv=(c[0].uv&0xffff)|(clut<<16);c[1].uv=(c[1].uv&0xffff)|(tpage<<16);
            for(int j=2;j<count;j++) c[j].uv&=0xffff;
            pk=tmd_emit(pk,c,count,1,gouraud,tag,(color&0xff000000u)|(gouraud?c[0].color&0xffffffu:color&0xffffffu));
        }
    } else for(int i=0;i<4;i++) pk=tmd_subdivide(pk,child[i],depth+1,limit,count,gouraud,tag,color,clut,tpage,w,h);
    return pk;
}
static PACKET *tmd_div(void *prim,SVECTOR *verts,SVECTOR *norms,PACKET *pk,unsigned n,int shift,
                       GsOT *ot,unsigned long *scratch,int count,int gouraud,int mode)
{
    if(!n) return pk;
    const unsigned char *p=prim;unsigned size=tmd_size(p[3],p[2]);
    for(unsigned k=0;k<n;k++,p+=size) {
        TmdVertex v[4]={0};unsigned normals[4];tmd_decode(p,verts,norms,count,1,gouraud,mode,v,normals);
        tmd_project3(v);gte_command(GTE_CMD_NCLIP);long clip=gte_mfc2(GTE_MAC0);
        if((int32_t)clip<=0 && !(count==3&&!gouraud&&(p[2]&2))) continue;
        if(count==4) tmd_project1(v+3);
        gte_command(count==4?GTE_CMD_AVSZ4:GTE_CMD_AVSZ3);unsigned z=gte_mfc2(GTE_OTZ);
        uint32_t color;
        if(mode==2) {color=0x808080u|((unsigned)(p[3]|ton*2)<<24);for(int i=0;i<count;i++) v[i].color=0x808080;}
        else {
            color=tmd_color(p,v,norms,normals,count,1,gouraud,mode);
            if(mode==1) color=(color&0xffffffu)|((unsigned)(p[3]|ton*2)<<24);
        }
        unsigned index=(z-ot->offset)>>(shift&31);unsigned long *tag=(unsigned long *)ot->org+index;
        scratch[3]=half(p+6)|(unsigned)half(p+10)<<16;scratch[4]=color;scratch[5]=(uintptr_t)tag;
        memcpy((char *)scratch+24,v,count*sizeof(*v));
        PORT_ASSERT(scratch[0]>0&&scratch[0]<=7);
        pk=tmd_subdivide(pk,v,0,scratch[0],count,gouraud,tag,color,half(p+6),half(p+10),scratch[1],scratch[2]);
    }
    return pk;
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastF3L */
PACKET *GsTMDfastF3L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,0,0,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastNF4 */
PACKET *GsTMDfastNF4(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,NULL,pk,n,shift,ot,4,0,0,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastF4NL */
PACKET *GsTMDfastF4NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,0,0,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastF4L */
PACKET *GsTMDfastF4L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,0,0,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastG3L */
PACKET *GsTMDfastG3L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,0,1,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastG4L */
PACKET *GsTMDfastG4L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,0,1,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTNF3 */
PACKET *GsTMDfastTNF3(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,NULL,pk,n,shift,ot,3,1,0,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTF3NL */
PACKET *GsTMDfastTF3NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,1,0,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTF3L */
PACKET *GsTMDfastTF3L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,1,0,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTNF4 */
PACKET *GsTMDfastTNF4(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,NULL,pk,n,shift,ot,4,1,0,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTF4NL */
PACKET *GsTMDfastTF4NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,1,0,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTF4L */
PACKET *GsTMDfastTF4L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,1,0,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTNG3 */
PACKET *GsTMDfastTNG3(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,NULL,pk,n,shift,ot,3,1,1,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTG3NL */
PACKET *GsTMDfastTG3NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,1,1,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTG3L */
PACKET *GsTMDfastTG3L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,3,1,1,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTNG4 */
PACKET *GsTMDfastTNG4(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,NULL,pk,n,shift,ot,4,1,1,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTG4NL */
PACKET *GsTMDfastTG4NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,1,1,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDfastTG4L */
PACKET *GsTMDfastTG4L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    (void)scratch;return tmd_fast(prim,verts,norms,pk,n,shift,ot,4,1,1,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTF3NL */
PACKET *GsTMDdivTF3NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,norms,pk,n,shift,ot,scratch,3,0,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTNF3 */
PACKET *GsTMDdivTNF3(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,NULL,pk,n,shift,ot,scratch,3,0,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTG3NL */
PACKET *GsTMDdivTG3NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,norms,pk,n,shift,ot,scratch,3,1,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTNG3 */
PACKET *GsTMDdivTNG3(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,NULL,pk,n,shift,ot,scratch,3,1,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTF4L */
PACKET *GsTMDdivTF4L(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,norms,pk,n,shift,ot,scratch,4,0,0);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTF4NL */
PACKET *GsTMDdivTF4NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,norms,pk,n,shift,ot,scratch,4,0,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTNF4 */
PACKET *GsTMDdivTNF4(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,NULL,pk,n,shift,ot,scratch,4,0,1);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTG4NL */
PACKET *GsTMDdivTG4NL(void *prim, SVECTOR *verts, SVECTOR *norms, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,norms,pk,n,shift,ot,scratch,4,1,2);
}

/* asm:asm/us/main/psyq/libgte.s GsTMDdivTNG4 */
PACKET *GsTMDdivTNG4(void *prim, SVECTOR *verts, PACKET *pk, unsigned n, int shift, GsOT *ot, unsigned long *scratch)
{
    return tmd_div(prim,verts,NULL,pk,n,shift,ot,scratch,4,1,1);
}

/* asm:asm/us/main/psyq/libgs.s GsSortObject4J */
void GsSortObject4J(GsDOBJ2 *obj,GsOT *ot,int shift,unsigned long *scratch)
{
    clock_psyq_entry();unsigned a=obj->attribute;if(a>>31) return;ton=(a>>30)&1;
    unsigned div=(a>>9)&7,mode=0;
    if(a&64) mode=2;else if((a&32)?(a&8):(light_mode&1)) mode=1;
    if(div) {scratch[0]=div;scratch[1]=width;scratch[2]=height;}
    unsigned long *o=obj->tmd;unsigned char *p=(void *)(uintptr_t)o[4];unsigned remaining=o[5];
    SVECTOR *verts=(void *)(uintptr_t)o[0],*norms=(void *)(uintptr_t)o[2];
    while(remaining) {
        unsigned n=half(p),code=p[3]&0xfd,flags=p[2];PORT_ASSERT(n&&n<=remaining);
        PACKET *(*fn)()=NULL;unsigned d=!!div;
        switch(code) {
        case 0x20:fn=(flags&4)?GsFCALL4.f3g[mode]:GsFCALL4.f3[d][mode];break;
        case 0x21:fn=GsFCALL4.nf3[d];break;
        case 0x28:fn=(flags&4)?GsFCALL4.f4g[mode]:GsFCALL4.f4[d][mode];break;
        case 0x29:fn=GsFCALL4.nf4[d];break;
        case 0x30:fn=(flags&4)?GsFCALL4.g3g[mode]:GsFCALL4.g3[d][mode];break;
        case 0x31:fn=GsFCALL4.ng3[d];break;
        case 0x38:fn=(flags&4)?GsFCALL4.g4g[mode]:GsFCALL4.g4[d][mode];break;
        case 0x39:fn=GsFCALL4.ng4[d];break;
        case 0x24:fn=GsFCALL4.tf3[d][mode];break;case 0x25:fn=GsFCALL4.ntf3[d];break;
        case 0x2c:fn=GsFCALL4.tf4[d][mode];break;case 0x2d:fn=GsFCALL4.ntf4[d];break;
        case 0x34:fn=GsFCALL4.tg3[d][mode];break;case 0x35:fn=GsFCALL4.ntg3[d];break;
        case 0x3c:fn=GsFCALL4.tg4[d][mode];break;case 0x3d:fn=GsFCALL4.ntg4[d];break;
        default:PORT_ASSERT(0);
        }
        PORT_ASSERT(fn);
        if(code&1) {
            typedef PACKET *(*NFn)(void *,SVECTOR *,PACKET *,unsigned,int,GsOT *,unsigned long *);
            GsOUT_PACKET_P=((NFn)fn)(p,verts,GsOUT_PACKET_P,n,shift,ot,scratch);
        } else {
            typedef PACKET *(*LFn)(void *,SVECTOR *,SVECTOR *,PACKET *,unsigned,int,GsOT *,unsigned long *);
            GsOUT_PACKET_P=((LFn)fn)(p,verts,norms,GsOUT_PACKET_P,n,shift,ot,scratch);
        }
        p+=n*tmd_size(code,flags);remaining-=n;
    }
}
