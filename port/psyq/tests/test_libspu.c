/* asm-derived API expectations, no disc. */
#include "../psyq.h"
#include "../clock.h"
#include "../../backend/spu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%d: %s\n",__LINE__,#c);exit(1); } } while(0)
int main(void)
{
    clock_reset();psyq_libspu_reset();
    CHECK(SpuGetKeyStatus(0)==-1);CHECK(SpuGetKeyStatus(1)==SPU_OFF);
    spu_write16(0x188,1);CHECK(SpuGetKeyStatus(1)==SPU_ON_ENV_OFF);
    clock_advance(768);CHECK(SpuGetKeyStatus(1)==SPU_ON);
    spu_write16(0x18c,1);CHECK(SpuGetKeyStatus(1)==SPU_OFF_ENV_ON);
    clock_advance(768*3);CHECK(SpuGetKeyStatus(1)==SPU_OFF);
    CHECK(SpuGetKeyStatus(0x1000000)==-1);
    CHECK(SpuSetReverb(1)==1);CHECK(spu_read16(0x1aa)&0x80);
    CHECK(SpuSetReverb(99)==1);CHECK(SpuSetReverb(0)==0);
    CHECK(SpuSetReverbVoice(8,0xff123456)==0x123456);
    CHECK(SpuSetReverbVoice(1,0x800001)==0x923457);
    CHECK(SpuSetReverbVoice(0,0x800007)==0x123450);
    CHECK(SpuSetReverbVoice(-1,0)==0x123450);
    SpuReverbAttr a={.mask=2,.depth={1234,-2345}};
    CHECK(SpuSetReverbDepth(&a)==0);CHECK(spu_read16(0x184)==1234);CHECK(spu_read16(0x186)==0);
    a.mask=4;SpuSetReverbDepth(&a);CHECK((int16_t)spu_read16(0x186)==-2345);
    a.mask=1;a.mode=1;CHECK(SpuSetReverbModeParam(&a)==0);
    CHECK(spu_read16(0x1a2)==0xfb28);CHECK(spu_read16(0x1c0)==0x7d);
    CHECK(spu_read16(0x1c4)==0x6d80);CHECK(spu_read16(0x1ce)==0xba80);
    CHECK(spu_read16(0x184)==0 && spu_read16(0x186)==0);
    a.mode=10;CHECK(SpuSetReverbModeParam(&a)==-1);CHECK(spu_read16(0x1a2)==0xfb28);
    a.mode=-1;CHECK(SpuSetReverbModeParam(&a)==-1);
    a.mode=7;SpuSetReverbModeParam(&a);
    uint16_t untouched=spu_read16(0x1c4);
    a.mask=8;a.delay=127;SpuSetReverbModeParam(&a);
    CHECK(spu_read16(0x1d4)==8192-spu_read16(0x1c0));
    CHECK(spu_read16(0x1d6)==4096-spu_read16(0x1c2));
    CHECK(spu_read16(0x1c4)==untouched);
    a.mask=16;a.feedback=64;SpuSetReverbModeParam(&a);CHECK(spu_read16(0x1ce)==64*33024/127);
    SpuSetReverb(0);memset(spu_ram,0xa5,SPU_RAM_SIZE);
    CHECK(SpuClearReverbWorkArea(1)==0);
    for(unsigned i=0;i<SPU_RAM_SIZE;i++) CHECK(spu_ram[i]==(i<0x7d940?0xa5:0));
    memset(spu_ram,0xa5,SPU_RAM_SIZE);CHECK(SpuClearReverbWorkArea(0)==0);
    CHECK(spu_ram[0x7ff7f]==0xa5 && spu_ram[0x7ff80]==0 && spu_ram[0x7ffff]==0);
    CHECK(SpuClearReverbWorkArea(-1)==-1);CHECK(SpuClearReverbWorkArea(10)==-1);
    a.mask=1;a.mode=0x101;SpuSetReverb(1);CHECK(SpuSetReverbModeParam(&a)==0);
    CHECK(spu_read16(0x1aa)&0x80);CHECK(spu_transfer_completed());
    CHECK(spu_ram[0x7d940]==0);
    /* Active ROOM reverb may only modify its work area. */
    memset(spu_ram,0x55,0x7d940);spu_write16(0x184,0x7fff);spu_write16(0x186,0x7fff);
    clock_advance(768*1024);spu_sync();
    for(unsigned i=0;i<0x7d940;i++) CHECK(spu_ram[i]==0x55);
    psyq_libspu_reset();CHECK(SpuSetReverb(-1)==0);CHECK(spu_samples()==clock_now()/768);
    puts("libspu L1: OK");return 0;
}
