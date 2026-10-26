/* ABI/packet expectations translated independently from asm/us/main/psyq/libgpu.s. */
#include "../psyq.h"
#define _LIBGPU_H_ /* Use the runtime ABI types while exercising the game bridge macro. */
#include <libgpu.h>
#include "../clock.h"
#include "../ps1mem.h"
#include "../../backend/gpu.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL libgpu:%d %s\n",__LINE__,#c);failures++;} } while(0)
static unsigned long ot[4], packets[3][4], cyclic[1];
static unsigned long data[1024*128], back[1024*128];
static uint32_t xy(int x,int y) { return (uint16_t)x|((uint32_t)(uint16_t)y<<16); }
static void helpers(void)
{
    CHECK(GetClut(1008,511)==0x7fff); CHECK(GetClut(-16,-1)==0xffff);
    CHECK(GetTPage(2,3,960,768)==0x97f); CHECK(GetTPage(7,7,-1,-1)==0x9ff);
    _Alignas(unsigned long) unsigned char buf[sizeof(POLY_GT4)];
    unsigned char expected[sizeof(buf)];
#define SETTER(fn,type,len,code) do { \
    memset(buf,0xa5,sizeof(buf));memset(expected,0xa5,sizeof(expected)); \
    expected[3]=len;expected[7]=code;fn((type*)buf);CHECK(!memcmp(buf,expected,sizeof(buf))); \
} while(0)
    SETTER(SetPolyFT3,POLY_FT3,7,0x24);SETTER(SetPolyF4,POLY_F4,5,0x28);
    SETTER(SetPolyFT4,POLY_FT4,9,0x2c);SETTER(SetPolyG4,POLY_G4,8,0x38);
    SETTER(SetPolyGT4,POLY_GT4,12,0x3c);SETTER(SetLineF2,LINE_F2,3,0x40);
    LINE_F3 l3;memset(&l3,0xa5,sizeof(l3));SetLineF3(&l3);CHECK(l3.tag==0x05a5a5a5&&l3.code==0x48&&l3.pad==0x55555555);
    LINE_F4 l4;memset(&l4,0xa5,sizeof(l4));SetLineF4(&l4);CHECK(l4.tag==0x06a5a5a5&&l4.code==0x4c&&l4.pad==0x55555555);
    SetSemiTrans(buf,7);CHECK(buf[7]==0x42);SetSemiTrans(buf,0);CHECK(buf[7]==0x40);
    unsigned short ofs[]={0xffff,0x800};DR_OFFSET off;memset(&off,0xa5,sizeof(off));SetDrawOffset(&off,ofs);
    CHECK(off.tag==0x02a5a5a5&&off.code[0]==0xe50007ff&&off.code[1]==0);
    DR_LOAD load;RECT r={-1,511,3,1};memset(&load,0xa5,sizeof(load));SetDrawLoad(&load,&r);
    CHECK(load.tag==0x06a5a5a5&&load.code[0]==0xa0000000&&load.code[1]==0x01ffffff&&load.code[2]==0x00010003);
    CHECK(load.p[1]==0xa5a5a5a5&&load.p[2]==0x01000000);
    r.w=24;SetDrawLoad(&load,&r);CHECK((load.tag>>24)==16&&load.p[12]==0x01000000);
    r.w=25;SetDrawLoad(&load,&r);CHECK(load.tag==0x01000000); /* asm overwrites tag on oversize */
    CHECK(SetGraphDebug(257)==0);CHECK(SetGraphDebug(0)==1);
    DISPENV disp;memset(&disp,0xa5,sizeof(disp));CHECK(SetDefDispEnv(&disp,0,240,320,240)==&disp);
    CHECK(disp.disp.y==240&&disp.disp.w==320&&disp.screen.w==0&&disp.pad1==0);
    disp.pad0=77;CHECK(PutDispEnv(&disp)==&disp&&disp.pad0==0);SetDispMask(1);
    uint32_t out[320*240];int w,h;CHECK(gpu_read_display(out,&w,&h)&&w==320&&h==240);SetDispMask(0);
    DRAWENV draw={0};draw.clip=(RECT){8,8,4,4};draw.ofs[0]=8;draw.ofs[1]=8;draw.dtd=1;draw.dfe=1;
    draw.tw=(RECT){8,16,8,16};draw.tpage=0x104;draw.isbg=1;draw.r0=255;
    CHECK(PutDrawEnv(&draw)==&draw);CHECK(draw.dr_env.tag==0x09ffffff);
    CHECK(draw.dr_env.code[0]==(0xe3000000|8|(8<<10)));
    CHECK(draw.dr_env.code[1]==(0xe4000000|11|(11<<10)));
    CHECK(draw.dr_env.code[2]==(0xe5000000|8|(8<<11)));
    CHECK(draw.dr_env.code[3]==0xe1000704);CHECK(draw.dr_env.code[4]==0xe20107df);
    CHECK(draw.dr_env.code[5]==0xe6000000&&draw.dr_env.code[6]==0x600000ff);
    CHECK(draw.dr_env.code[7]==0&&draw.dr_env.code[8]==0x00040004);
    CHECK(gpu_vram()[8*1024+8]==31);
    draw.isbg=0;PutDrawEnv(&draw);CHECK(draw.dr_env.tag==0x06ffffff);
}
static void tim(void)
{
    unsigned long file[64];TIM_IMAGE image;
    for(int mode=0;mode<=2;mode++)for(int clut=0;clut<=1;clut++) {
        memset(file,0,sizeof(file));int i=0;
        for(int n=0;n<2;n++) {
            file[i++]=0x10;file[i++]=mode|(clut?8:0);
            if(clut) {file[i++]=16;file[i++]=xy(16,200);file[i++]=xy(2,1);file[i++]=0x03e0001f;}
            file[i++]=16;file[i++]=xy(320,0);file[i++]=xy(2,1);file[i++]=0x7c004210;
        }
        CHECK(OpenTIM(file)==0);int start=0;
        for(int n=0;n<2;n++) {
            CHECK(ReadTIM(&image)==&image&&image.mode==(unsigned)(mode|(clut?8:0)));
            CHECK(image.crect==(clut?(RECT*)(file+start+3):NULL));CHECK(image.caddr==(clut?file+start+5:NULL));
            CHECK(image.prect==(RECT*)(file+start+(clut?7:3)));CHECK(image.paddr==file+start+(clut?9:5));
            CHECK(image.prect->x==320&&image.prect->w==2&&*image.paddr==0x7c004210);start+=clut?10:6;
        }
        TIM_IMAGE previous=image;CHECK(ReadTIM(&image)==NULL);CHECK(!memcmp(&previous,&image,sizeof(image)));
        file[start]=0x10;file[start+1]=2;file[start+2]=16;CHECK(ReadTIM(&image)==&image); /* cursor unchanged on bad magic */
    }
}
static void ordering(void)
{
    ResetGraph(0);gpu_fill(0,0,1024,512,0);CHECK(ClearOTagR(ot,4)==ot);CHECK(ot[0]==PS1_LINK_END);
    for(int i=1;i<4;i++)CHECK(ot[i]==PS1_LINK(&ot[i-1]));
    for(int i=0;i<3;i++) {
        packets[i][0]=0x03000000;packets[i][1]=0x60000000u|(0xffu<<(i*8));
        packets[i][2]=xy(8,8);packets[i][3]=xy(2,2);AddPrim(&ot[i],packets[i]);
        CHECK((ot[i]&PS1_LINK_END)==PS1_LINK(packets[i]));
    }
    DrawOTag(&ot[3]);CHECK(gpu_vram()[8*1024+8]==31); /* bucket 0 drawn last */
    ClearOTagR(ot,1);AddPrim(ot,packets[0]);AddPrim(ot,packets[1]);AddPrim(ot,packets[2]);
    DrawOTag(ot);CHECK(gpu_vram()[8*1024+8]==31); /* last added first drawn */
    /* The upstream macro must link packets without clobbering their lengths. */
    ClearOTagR(ot,1);
    for(int i=0;i<3;i++) {
        packets[i][0]=0x03000000;
        addPrim(ot,packets[i]);
        CHECK((packets[i][0]>>24)==3);
        CHECK((packets[i][0]&PS1_LINK_END)==(i?PS1_LINK(packets[i-1]):PS1_LINK_END));
        CHECK(*ot==PS1_LINK(packets[i]));
    }
    gpu_fill(0,0,1024,512,0);
    DrawOTag(ot);CHECK(gpu_vram()[8*1024+8]==31);
    static DR_LOAD load;
    RECT r={32,32,16,1};SetDrawLoad(&load,&r);
    for(int i=0;i<8;i++)load.p[i]=(unsigned long)(2*i+1)|((unsigned long)(2*i+2)<<16);
    ClearOTagR(ot,1);AddPrim(ot,&load);DrawOTag(ot);
    for(int i=0;i<16;i++)CHECK(gpu_vram()[32*1024+32+i]==i+1);
    cyclic[0]=PS1_LINK(cyclic);DrawOTag(cyclic); /* bounded cycle; diagnostic recorded in the evidence log */
}
static void transfers_and_time(void)
{
    clock_reset();CHECK(ResetGraph(0)==0);RECT r={1022,511,4,3};
    for(int i=0;i<6;i++) data[i]=0x10001u*(i+1);
    CHECK(LoadImage(&r,data)==0);CHECK(StoreImage(&r,back)==0);CHECK(!memcmp(data,back,24));
    CHECK(MoveImage(&r,11,11)==0);r.x=11;r.y=11;StoreImage(&r,back);CHECK(!memcmp(data,back,24));
    ClearImage(&r,255,128,0);StoreImage(&r,back);for(int i=0;i<6;i++)CHECK(back[i]==0x021f021f);
    r.w=0;CHECK(MoveImage(&r,0,0)==-1);
    r=(RECT){0,0,-1,512};ClearImage(&r,0,0,0);CHECK(r.w==0&&r.h==511);
    r=(RECT){0,0,1024,-1};ClearImage(&r,0,0,0);CHECK(r.w==1023&&r.h==0);
    /* Reset modes 3 and 5 preserve the GPU environment and display. */
    gpu_set_display(0,0,1,1,0);SetDispMask(1);
    uint32_t pixel;int width,height;
    ResetGraph(3);CHECK(gpu_read_display(&pixel,&width,&height)&&width==1&&height==1);
    ResetGraph(5);CHECK(gpu_read_display(&pixel,&width,&height));
    ResetGraph(0);CHECK(!gpu_read_display(&pixel,&width,&height));
    DrawSync(0);clock_reset();ResetGraph(0);
    r=(RECT){0,0,1024,256};memset(data,0,sizeof(data));uint64_t start=clock_now();CHECK(LoadImage(&r,data)==0);
    uint64_t end=start+CLOCK_PSYQ_ENTRY_CYCLES+57344u;
    CHECK(DrawSync(1)==1);CHECK(DrawSync(1)==1);CHECK(clock_now()<end);
    CHECK(DrawSync(0)==0&&clock_now()==end);CHECK(DrawSync(1)==0);
    /* Redux: 131072 uploaded words / 16 words per block * 7 = 57344 cycles.
     * Outstanding work accumulates even though CPU-side execution is immediate. */
    start=clock_now();LoadImage(&r,data);LoadImage(&r,data);
    end=start+CLOCK_PSYQ_ENTRY_CYCLES+2*(57344u);
    CHECK(DrawSync(0)==0&&clock_now()==end);
    for(int i=0;i<128;i++)data[i]=(unsigned long)(2*i+1)|((unsigned long)(2*i+2)<<16);
    start=clock_now();CHECK(LoadClut(data,32,256)==0x4002);
    CHECK(clock_now()==start+CLOCK_PSYQ_ENTRY_CYCLES);
    for(int i=0;i<256;i++)CHECK(gpu_vram()[256*1024+32+i]==i+1);
    DrawSync(0);
    /* Repeated polling reaches idle; a subsequent blocking sync returns successfully. */
    LoadImage(&r,data);while(DrawSync(1)) {} CHECK(DrawSync(0)==0);
    ResetGraph(0);CHECK(DrawSync(1)==0);
}
int main(void)
{
    clock_reset();ResetGraph(0);helpers();tim();ordering();transfers_and_time();
    if(failures)fprintf(stderr,"libgpu: %d failures\n",failures);else puts("libgpu: packets, TIM, OT and cycle timing passed");
    return failures?1:0;
}
