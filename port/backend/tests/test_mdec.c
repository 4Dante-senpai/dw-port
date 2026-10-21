#include "../mdec.h"
#include "../../host/pack/sha256.h"
#include "mdec_vectors.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"mdec:%d: %s\n",__LINE__,#c); exit(1); } } while(0)
int main(void)
{
    uint8_t quant[128]; int16_t scale[64];
    for (unsigned i=0;i<128;i++) quant[i]=((i%64)*7+(i>=64?11:0))%253+1;
    for (int u=0;u<8;u++) for(int x=0;x<8;x++) scale[u*8+x]=((x*31+u*17+13)%63-31)*512+(u*3+x)%8;
    mdec_reset();
    CHECK(mdec_command(0x40000001,quant,31)==-1);
    CHECK(mdec_command(0x40000001,quant,32)==0);
    CHECK(mdec_command(0x60000000,scale,31)==-1);
    CHECK(mdec_command(0x60000000,scale,32)==0);
    unsigned index=0;
    const int dc[6]={-511,511,-100,100,7,-7};
    for(unsigned qi=0;qi<3;qi++) {
        unsigned q=qi==2?63:qi;
        uint16_t stream[26]; stream[0]=0xfe00; stream[1]=0xfe00;
        for(unsigned b=0;b<6;b++) {
            stream[2+b*4]=(q<<10)|(dc[b]&1023);
            stream[3+b*4]=511;
            stream[4+b*4]=(3<<10)|512;
            stream[5+b*4]=0xfe00;
        }
        for(unsigned rgb=0;rgb<2;rgb++) for(unsigned stp=0;stp<2;stp++) {
            uint32_t cmd=0x2000000d|((rgb?2u:3u)<<27)|(stp<<25);
            unsigned words=rgb?192:128;
            uint32_t output[194], split[192];
            memset(output,0xa5,sizeof(output));
            CHECK(!mdec_command(cmd,stream,13));
            mdec_out(output+1,words);
            CHECK(output[0]==0xa5a5a5a5 && output[words+1]==0xa5a5a5a5);
            Sha256 sha; uint8_t hash[32]; char hex[65];
            Sha256_Init(&sha); Sha256_Update(&sha,output+1,words*4); Sha256_Final(&sha,hash);
            for(unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",hash[i]);
            if (strcmp(hex,mdec_hashes[index])) fprintf(stderr,"vector %u actual=%s expected=%s\n",index,hex,mdec_hashes[index]);
            CHECK(!strcmp(hex,mdec_hashes[index++]));
            CHECK(!mdec_command(cmd,stream,13));
            mdec_out(split,7); mdec_out(split+7,words-7);
            CHECK(!memcmp(output+1,split,words*4));
        }
    }
    uint16_t empty[2]={0xfe00,0xfe00}; uint32_t out[192];
    CHECK(!mdec_command(0x30000001,empty,1)); mdec_out(out,192);
    for(unsigned i=0;i<192;i++) CHECK(out[i]==0);
    puts("mdec: 12 independent IDCT/color/STP hashes, partial FIFO, EOB, saturation OK");
    return 0;
}
