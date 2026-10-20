/* L1: real Pack I/O, sector cadence, ring backpressure, XA routing and EOF. */
#include "stream_fixture.h"
#include "../../backend/cdrom.h"
#include "../../backend/spu.h"
#include "../../backend/xa.h"
#include "../psyq.h"
#include "../clock.h"
#include "../libcd_stream.h"
#ifdef __EMSCRIPTEN__
/* Node L1 supplies the worker's positional I/O seam with synthetic MEMFS bytes.
 * Browser mailbox behavior has its own worker tests. */
uint64_t io_size(void)
{
    FILE *f=fopen("stream.pack","rb"); CHECK(f);
    CHECK(!fseek(f,0,SEEK_END)); long n=ftell(f); CHECK(n>0); fclose(f); return n;
}
int io_read(uint64_t offset, void *out, uint32_t length)
{
    FILE *f=fopen("stream.pack","rb"); CHECK(f);
    int ok=!fseek(f,(long)offset,SEEK_SET) && fread(out,1,length,f)==length;
    fclose(f); return ok ? 0 : -1;
}
#define STREAM_PACK "dw1/us.pack"
#else
#define STREAM_PACK "stream.pack"
#endif
static unsigned long ringbuf[3*2048/4];
static int16_t decoded[2352*2];
static unsigned rendered, nonzero;
static void check_output(const int16_t *pcm, unsigned frames, void *arg)
{
    (void)arg;
    for (unsigned i=0;i<frames;i++,rendered++) for (unsigned ch=0;ch<2;ch++) {
        /* CdInit: CD volume 0x3fff, main direct volume 0x3fff (level 0x7ffe). */
        int expected=rendered<2352 ? ((decoded[rendered*2+ch]*0x3fff)>>15) : 0;
        expected=(expected*0x7ffe)>>15;
        CHECK(pcm[i*2+ch]==expected);
        nonzero+=pcm[i*2+ch]!=0;
    }
}
static void check_playback(int pause)
{
    clock_reset(); spu_reset(); psyq_libcd_reset(); CHECK(CdInit());
    StSetRing(ringbuf,1); StSetStream(1,1,(unsigned long)-1,NULL,NULL);
    CdlLOC loc; CdIntToPos(27,&loc); CdControl(CdlSetloc,(uint8_t *)&loc,NULL);
    CHECK(CdRead2(0x1e0)); clock_advance(CLOCK_HZ/150);
    CHECK(XaDec_Queued()==2352);
    rendered=nonzero=0; spu_set_output(check_output,NULL);
    if (pause) {
        CdControl(CdlPause,NULL,NULL);
        CHECK(rendered+(unsigned)XaDec_Queued()==2352);
        clock_advance(CLOCK_HZ/150*12); spu_sync();
        CHECK(rendered>2352 && !XaDec_Queued() && nonzero);
        CHECK(((StHEADER *)ringbuf)->id==0); /* No video sector after Pause. */
    } else {
        clock_advance(CLOCK_HZ/150*2); /* EOF after the reserved sector. */
        CHECK(rendered>0 && rendered<2352 && nonzero);
        unsigned before=rendered;
        uint64_t synced=spu_samples();
        clock_advance(128*768);
        CHECK(CdRead2(0x1e0)); /* Must drain owed PCM despite rt.active == 0. */
        CHECK(rendered-before==clock_now()/768-synced);
        CHECK(rendered-before>=128 && !XaDec_Queued());
    }
    spu_set_output(NULL,NULL); StUnSetRing();
}
static void video(unsigned lba, unsigned frame)
{
    uint8_t *p=user(lba); memset(p,0,2048);
    wr32(p,0x80010160); p[6]=1; wr32(p+8,frame); wr32(p+12,2016);
    p[16]=64; p[18]=16;
    memset(p+32,frame,2016);
}
int main(void)
{
    CHECK(!setenv("DW1_TRACE","stream.trace",1));
    make_disc(); write_cue("MODE2/2352");
    video(26,1); video(28,2);
    for (unsigned lba=27;lba<=29;lba+=2) {
        uint8_t *s=disc+lba*RAW;
        memset(s+16,0,2336); s[18]=s[22]=0x64;
        s[19]=s[23]=lba==27 ? 1 : 0x81; /* reserved coding */
        if (lba==27) for (unsigned g=0;g<18;g++) {
            uint8_t *group=s+24+128*g;
            memset(group,8,16); /* Filter 0, shift 8. */
            for (unsigned i=16;i<128;i++) group[i]=(uint8_t)(i*37+g*11);
        }
    }
    write_file("pack_t.bin",disc,sizeof(disc));
    CHECK(!pack_manifest("pack_t.cue","stream.tsv",err,sizeof(err)));
    CHECK(!pack_build("pack_t.cue","stream.tsv","stream.pack",err,sizeof(err)));
    size_t n; uint8_t *manifest=read_file("stream.tsv",&n), hash[32]; char hex[65];
    Sha256 sha; Sha256_Init(&sha); Sha256_Update(&sha,manifest,n); Sha256_Final(&sha,hash); free(manifest);
    for (unsigned i=0;i<32;i++) snprintf(hex+2*i,3,"%02x",hash[i]);
    CHECK(!cdrom_open(STREAM_PACK,hex,err,sizeof(err)));
    uint8_t audio[CDROM_SECTOR_SIZE1];
    CHECK(cdrom_stream_sector(27,audio)>0);
    XaDec_Reset(); CHECK(XaDec_Sector(audio+4));
    for (unsigned i=0;i<2352;i++) XaDec_Pop(&decoded[i*2],&decoded[i*2+1]);
    clock_reset(); spu_reset(); psyq_libcd_reset();
    CHECK(CdInit());
    CHECK(spu_read16(0x1b0)==0x3fff && spu_read16(0x1b2)==0x3fff);
    CHECK(spu_read16(0x1aa)==0xc001);
    StSetRing(ringbuf,1); StSetStream(1,1,(unsigned long)-1,NULL,NULL);
    CdlLOC loc; CdIntToPos(26,&loc); CdControl(CdlSetloc,(uint8_t *)&loc,NULL);
    CHECK(CdRead2(0x1e0)==1);
    uint64_t t=clock_now(); unsigned long *data,*head;
    clock_advance(CLOCK_HZ/150-1);
    CHECK(((StHEADER *)ringbuf)->id==0);
    clock_advance(1); CHECK(((StHEADER *)ringbuf)->id==2);
    CHECK(StGetNext(&data,&head)==0 && ((StHEADER *)head)->frameCount==1);
    CHECK(((uint8_t *)data)[0]==1 && ((uint8_t *)data)[2015]==1);
    clock_advance(CLOCK_HZ/150); CHECK(XaDec_Queued()==2352);
    clock_advance(CLOCK_HZ/150*3); /* video frame 2 waits for borrowed frame 1 */
    CHECK(((StHEADER *)head)->frameCount==1 && XaDec_Queued()==2352);
    CHECK(StGetNext(&data,&head)==1);
    CHECK(StFreeRing(data)==0); CHECK(StFreeRing(data)==1);
    clock_advance(CLOCK_HZ/150);
    CHECK(StGetNext(&data,&head)==0 && ((StHEADER *)head)->frameCount==2);
    CHECK(((uint8_t *)data)[2015]==2); CHECK(StFreeRing(data)==0);
    clock_advance(CLOCK_HZ/150);
    CHECK(StGetNext(&data,&head)==0 && ((StHEADER *)head)->frameCount==0x7fffffff);
    CHECK(clock_now()>=t+4*(CLOCK_HZ/150));
    uint8_t *trace=read_file("stream.trace",&n);
    CHECK(strstr((char *)trace,"xa rejected coding=0x81 lba=29\n")); free(trace);
    StUnSetRing(); CdControl(CdlPause,NULL,NULL);
    StSetRing(ringbuf,1); StSetStream(1,1,(unsigned long)-1,NULL,NULL);
    CdControl(CdlSetloc,(uint8_t *)&loc,NULL); CHECK(CdRead2(0x160));
    clock_advance(CLOCK_HZ/75-1); CHECK(((StHEADER *)ringbuf)->id==0);
    clock_advance(1); CHECK(((StHEADER *)ringbuf)->id==2);
    CdControl(CdlPause,NULL,NULL); clock_advance(CLOCK_HZ/75*3);
    CHECK(!XaDec_Queued()); /* Pause cancels the pending audio sector. */
    StUnSetRing();
    /* A CdRead superseded by streaming completes with an aborted result. */
    unsigned long readbuf[2048/4]; memset(readbuf,0x5a,sizeof(readbuf));
    CHECK(CdRead(1,readbuf,CdlModeSpeed)); CHECK(CdReadSync(1,NULL)==1);
    CHECK(CdRead2(0x1e0));
    CHECK(CdReadSync(1,NULL)==-1 && CdReadSync(0,NULL)==-1);
    CdControl(CdlPause,NULL,NULL); clock_advance(CLOCK_HZ);
    for (unsigned i=0;i<sizeof(readbuf);i++) CHECK(((uint8_t *)readbuf)[i]==0x5a);
    CHECK(CdRead(1,readbuf,CdlModeSpeed)); CHECK(CdReadSync(0,NULL)==0);
    check_playback(1);
    check_playback(0);
    uint8_t sector[CDROM_SECTOR_SIZE1];
    CHECK(cdrom_stream_sector(22,sector)>0 && !(sector[6]&0x24));
    cdrom_close();
    /* Multi-chunk publication, contiguous wrap, then incomplete EOF. */
    StSetRing(ringbuf,3); StSetStream(1,1,(unsigned long)-1,NULL,NULL);
    memset(sector,0,sizeof(sector)); sector[1]=2; sector[2]=9;
    uint8_t *v=sector+12;
    wr32(v,0x80010160); v[6]=2; wr32(v+8,3); v[16]=64; v[18]=16;
    memset(v+32,3,2016); CHECK(cd_stream_sector(sector));
    CHECK(((StHEADER *)ringbuf)->id==3);
    v[4]=1; sector[2]=0x10; memset(v+32,4,2016); CHECK(cd_stream_sector(sector));
    CHECK(StGetNext(&data,&head)==0 && ((StHEADER *)head)->frameCount==3);
    CHECK(((uint8_t *)data)[0]==3 && ((uint8_t *)data)[2016]==4);
    CHECK(StGetBackloc(&loc)==3 && loc.minute==0 && loc.second==2 && loc.sector==0x11);
    CHECK(!StFreeRing(data));
    v[4]=0; wr32(v+8,4); CHECK(cd_stream_sector(sector));
    v[4]=1; CHECK(cd_stream_sector(sector)); /* producer wraps from slot 2 to 0 */
    CHECK(!StGetNext(&data,&head) && ((StHEADER *)head)->frameCount==4);
    CHECK(!StFreeRing(data));
    v[4]=0; wr32(v+8,5); CHECK(cd_stream_sector(sector));
    cd_stream_eof();
    CHECK(!StGetNext(&data,&head) && ((StHEADER *)head)->frameCount==0x7fffffff);
    StUnSetRing();
    puts("cd_stream: sector cadence, XA routing, full ring, free and EOF OK"); return 0;
}
