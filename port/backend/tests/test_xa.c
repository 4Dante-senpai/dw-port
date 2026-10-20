/* L1: independent golden SHA-256 vectors for every XA coding combination. */
#include "../xa.h"
#include "../../host/pack/sha256.h"
#include "xa_vectors.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"XA:%d: %s\n",__LINE__,#c); exit(1); } } while(0)
int main(void)
{
    uint8_t body[2336]={0};
    unsigned coding[8]={0,1,4,5,16,17,20,21};
    for (unsigned k=0;k<8;k++) {
        XaDec_Reset(); Sha256 h; Sha256_Init(&h);
        for (unsigned sector=0;sector<2;sector++) {
            body[2]=0x24; body[3]=coding[k];
            for (unsigned g=0;g<18;g++) {
                uint8_t *p=body+8+128*g;
                for (unsigned i=0;i<16;i++) p[i]=0;
                for (unsigned u=0;u<8;u++) p[4+u]=((g+u)%4)*16+(g+u)%16;
                for (unsigned i=16;i<128;i++) p[i]=(g*128+i)*37+sector*53+11;
            }
            CHECK(XaDec_Sector(body)==1);
            unsigned frames=(coding[k]&16 ? 2352 : 4704)*(coding[k]&1 ? 1 : 2)*(coding[k]&4 ? 2 : 1)/2;
            CHECK((unsigned)XaDec_Queued()==frames);
            while (XaDec_Queued()) {
                int16_t l,r; XaDec_Pop(&l,&r);
                uint8_t bytes[4]={(uint8_t)l,(uint8_t)(l>>8),(uint8_t)r,(uint8_t)(r>>8)};
                Sha256_Update(&h,bytes,4);
            }
        }
        uint8_t hash[32]; char hex[65]; Sha256_Final(&h,hash);
        for (int i=0;i<32;i++) snprintf(hex+i*2,3,"%02x",hash[i]);
        CHECK(!strcmp(hex,hashes[k]));
    }
    XaDec_Reset();
    for (unsigned c=0;c<256;c++) if (c&0xaa) {
        body[3]=c; CHECK(!XaDec_Sector(body)); CHECK(XaDec_Queued()==0);
    }
    body[3]=1; body[2]=0x04; CHECK(!XaDec_Sector(body));
    body[2]=0x20; CHECK(!XaDec_Sector(body));
    int16_t l=1,r=1; XaDec_Pop(&l,&r); CHECK(!l && !r);
    FILE *wav=tmpfile(); CHECK(wav);
    XaDec_Reset(); XaDec_CaptureBegin(wav); body[2]=0x24;
    CHECK(XaDec_Sector(body)); XaDec_CaptureEnd();
    CHECK(!fseek(wav,0,SEEK_END)); CHECK(ftell(wav)==44+2352*4);
    rewind(wav); uint8_t header[44]; CHECK(fread(header,1,44,wav)==44);
    CHECK(!memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVEfmt ",8));
    CHECK(header[22]==2 && header[24]==0x44 && header[25]==0xac && header[34]==16);
    CHECK(!fclose(wav));
    puts("xa: 8 independent PCM hashes, coding rejection and empty FIFO OK");
    return 0;
}
