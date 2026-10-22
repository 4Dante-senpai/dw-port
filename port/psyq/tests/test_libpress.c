#include "../psyq.h"
#include "../libpress.h"
#include "../clock.h"
#include "../../backend/cdrom.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"libpress:%d %s\n",__LINE__,#c); exit(1); } } while(0)
static int calls, depth;
static unsigned long output[192];
static void done(void) {
    CHECK(depth++==0); CHECK(clock_in_handler());
    if(++calls<1000) DecDCTout(output,192);
    depth--;
}
static int finished(void *arg) { (void)arg; return calls==1000; }
static void bits(unsigned long *stream, const char *text) {
    uint16_t *p=(uint16_t *)stream+4;
    for(unsigned i=0;text[i];i++) if(text[i]=='1') p[i/16]|=(uint16_t)(1u<<(15-i%16));
}
static void vlc_variants(void) {
    uint16_t *table=calloc(0x8800,2); CHECK(table);
    uint32_t *ac=(uint32_t *)(table+0x400);
    unsigned long stream[32], out[128];
    /* Differential DC: six blocks, zero deltas, then v3 sentinel. */
    for(unsigned i=0;i<512;i++) ((uint32_t *)table)[i]=1; /* prefix=1, width=0 */
    for(unsigned i=0;i<8192;i++) ac[i*2]=0xfe000002;
    memset(stream,0,sizeof(stream)); stream[0]=0x38000020; stream[1]=0x00030002;
    bits(stream,"0100100100100100101111111111");
    CHECK(!DecDCTvlc2(stream,out,table));
    for(unsigned i=0;i<6;i++) CHECK(((uint16_t *)out)[2+i*2]==0x800 && ((uint16_t *)out)[3+i*2]==0xfe00);
    /* Unknown versions follow the same asm branch; no invented rejection. */
    stream[1]=0x00090002; CHECK(!DecDCTvlc2(stream,out,table));
    /* Signed differential amplitudes and separate Cr/Cb/Y predictors. */
    for(unsigned i=0;i<512;i++) ((uint32_t *)table)[i]=0x00020001;
    memset(stream,0,sizeof(stream));stream[0]=0x38000020;stream[1]=0x00030002;
    bits(stream,"0101000110011100101000010011101111111111");
    CHECK(!DecDCTvlc2(stream,out,table));
    const unsigned expected_dc[]={8,1016,12,20,8,20};
    for(unsigned i=0;i<6;i++) CHECK(((uint16_t *)out)[2+i*2]==(0x800|expected_dc[i]));
    /* A primary lookup can emit three run-level codes in one operation. */
    for(unsigned i=0;i<8192;i++) { ac[i*2]=0x00010002;ac[i*2+1]=0xfe000401; }
    memset(stream,0,sizeof(stream));stream[0]=0x38000020;stream[1]=0x00020001;
    bits(stream,"0000000000100111111111");
    CHECK(!DecDCTvlc2(stream,out,table));
    CHECK(((uint16_t *)out)[3]==1 && ((uint16_t *)out)[4]==0x401 && ((uint16_t *)out)[5]==0xfe00);
    for(unsigned i=0;i<8192;i++) { ac[i*2]=0xfe000002;ac[i*2+1]=0; }
    /* Escape: six prefix bits, one literal run-level halfword, then EOB. */
    for(unsigned i=128;i<256;i++) ac[i*2]=0x7c1f0006;
    memset(stream,0,sizeof(stream));stream[0]=0x38000020;stream[1]=0x00020001;
    bits(stream,"00000000000000010000000000000001100111111111");
    CHECK(!DecDCTvlc2(stream,out,table));
    CHECK(((uint16_t *)out)[2]==0x400 && ((uint16_t *)out)[3]==1 && ((uint16_t *)out)[4]==0xfe00);
    /* Extended AC lookup consumes eight bits before a nine-bit index. */
    memset(ac,0,0x10000);
    for(unsigned i=0;i<512;i++) ((uint32_t *)((uint8_t *)ac+0x10000))[i]=0xfe000002;
    memset(stream,0,sizeof(stream)); stream[0]=0x38000020;stream[1]=0x00020001;
    bits(stream,"000000000000000000100111111111");
    CHECK(!DecDCTvlc2(stream,out,table)); CHECK(((uint16_t *)out)[3]==0xfe00);
    free(table);
}
static void table_build(void) {
    size_t n; libpress_table("data_2",&n); uint8_t *data=calloc(1,n);CHECK(data);
    /* One literal followed by distance-one copies, then terminator. */
    size_t at=0;data[at++]=0;data[at++]=0x5a;data[at++]=0xf1;data[at++]=0;
    unsigned remaining=0x11000-1;
    while(remaining) { unsigned count=remaining>240?240:remaining;data[at++]=count-1;remaining-=count; }
    data[at++]=0xff;data[at++]=0xff; CHECK(at<=n);
    CHECK(!libpress_set_table("data_2",data,n));free(data);
    uint16_t *table=calloc(0x8802,2);CHECK(table);table[0x8800]=0xcafe;
    DecDCTvlcBuild(table);
    for(unsigned i=0;i<0x8800;i++) CHECK(table[i]==((i/4)%2?0:0x5a5a));
    CHECK(table[0x8800]==0xcafe);free(table);
}
#ifndef __EMSCRIPTEN__
static void put32(uint8_t *p, uint32_t v) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void short_executable(void) {
    uint8_t bytes[2052]={0};char error[256];
    memcpy(bytes,"DW1PACK",8);put32(bytes+8,1);put32(bytes+12,1);
    put32(bytes+48,64);put32(bytes+52,2048);
    memcpy(bytes+64,PACK_EXE_PATH,sizeof(PACK_EXE_PATH));
    put32(bytes+128,100);put32(bytes+132,1);put32(bytes+136,PACK_F1);
    put32(bytes+140,4);put32(bytes+144,2048);
    FILE *file=fopen("press-short.pack","wb");CHECK(file);
    CHECK(fwrite(bytes,1,sizeof(bytes),file)==sizeof(bytes));CHECK(!fclose(file));
    CHECK(!cdrom_open("press-short.pack",NULL,error,sizeof(error)));
    CHECK(libpress_load_tables(error,sizeof(error))==-1 && strstr(error,"missing or short"));
    size_t n; const uint8_t *iq=libpress_table("mdec_iq",&n);
    for(size_t i=0;i<n;i++) CHECK(iq[i]==0);
    cdrom_close();CHECK(!remove("press-short.pack"));
}
#endif
int main(void) {
    char error[256]; size_t n;
    CHECK(libpress_load_tables(error,sizeof(error))==-1);
    CHECK(strstr(error,"missing or short"));
    CHECK(!libpress_table("absent",&n) && !n);
    const char *names[]={"mdec_iq","mdec_coef"};
    for(unsigned i=0;i<2;i++) {
        libpress_table(names[i],&n); uint32_t *data=calloc(1,n); CHECK(data);
        data[0]=i?0x60000000:0x40000001;
        CHECK(libpress_set_table(names[i],data,n-1)==-1);
        CHECK(!libpress_set_table(names[i],data,n)); free(data);
    }
#ifndef __EMSCRIPTEN__
    short_executable();
    /* Restore the two synthetic upload commands after the failed load cleared them. */
    for(unsigned i=0;i<2;i++) {
        libpress_table(names[i],&n); uint32_t *data=calloc(1,n);CHECK(data);
        data[0]=i?0x60000000:0x40000001;CHECK(!libpress_set_table(names[i],data,n));free(data);
    }
#endif
    clock_reset(); DecDCTReset(0);
    unsigned long stream[2]={0x38000001,0xfe00fe00};
    DecDCTin(stream,3); CHECK(stream[0]==0x32000001);
    DecDCToutCallback(done); DecDCTout(output,192);
    CHECK(!calls); clock_wait_until(finished,NULL); CHECK(calls==1000 && !depth);
    CHECK(DecDCToutCallback(NULL)==(int)(uintptr_t)done);
    DecDCTout(output,192); DecDCTReset(1); clock_advance(100000); CHECK(calls==1000);
    /* Synthetic expanded VLC table: every AC is EOB after two bits. */
    uint16_t *table=calloc(0x8800,2); CHECK(table);
    uint32_t *ac=(uint32_t *)(table+0x400);
    for(unsigned i=0;i<8192;i++) ac[2*i]=0xfe000002;
    unsigned long bs[8]={0x38000020,0x00020001,0,0,0,0,0,0}, out[128];
    /* DC=0, EOB=10, DC terminator=0111111111, in MSB-first halfwords. */
    ((uint16_t *)bs)[4]=0x0027; ((uint16_t *)bs)[5]=0xfc00;
    memset(out,0xa5,sizeof(out)); CHECK(!DecDCTvlc2(bs,out,table));
    CHECK(out[0]==bs[0] && ((uint16_t *)out)[2]==0x400 && ((uint16_t *)out)[3]==0xfe00);
    for(unsigned i=4;i<69;i++) CHECK(((uint16_t *)out)[i]==0xfe00);
    free(table); vlc_variants(); table_build(); puts("libpress: tables, asynchronous reentrancy, reset and VLC OK"); return 0;
}
