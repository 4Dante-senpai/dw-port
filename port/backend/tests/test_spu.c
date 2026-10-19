/* L1 expectations: tools/port/audio/spu_vectors.py, independent psx-spx equations. */
#include "../spu.h"
#include "../../psyq/clock.h"
#include "../../psyq/psyq.h"
#include "../../host/pack/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__EMSCRIPTEN__) && !defined(NDEBUG)
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c); exit(1); } } while (0)
static Sha256 sha;
static unsigned output_frames;
static unsigned tail_nonzero;
static int16_t last[2];
static void collect(const int16_t *p, unsigned n, void *arg)
{
    (void)arg;
    for (unsigned i=0;i<n*2;i++) {
        if (output_frames + i/2 >= 64 && p[i]) tail_nonzero++;
        uint8_t le[2]={(uint8_t)p[i],(uint8_t)((uint16_t)p[i]>>8)};
        Sha256_Update(&sha,le,2);
    }
    last[0]=p[(n-1)*2];last[1]=p[(n-1)*2+1];
    output_frames += n;
}
static void hash(const char *name, const char *expected)
{
    uint8_t d[32]; char hex[65];
    Sha256_Final(&sha,d);
    for(int i=0;i<32;i++) sprintf(hex+2*i,"%02x",d[i]);
    if(strcmp(hex,expected)) fprintf(stderr,"%s got %s expected %s\n",name,hex,expected);
    CHECK(!strcmp(hex,expected));
}
static void reset(void) { clock_reset(); spu_reset(); Sha256_Init(&sha); output_frames=tail_nonzero=0; }
static void samples(unsigned n) { clock_advance((uint64_t)n*768); spu_sync(); }
static void voice(uint16_t a1,uint16_t a2)
{
    spu_write16(6,0x200); spu_write16(8,a1); spu_write16(10,a2);
    spu_write16(0x188,1);
}
static void envelope(const char *name,uint16_t a1,uint16_t a2,unsigned warmup,int release,unsigned n,const char *expected)
{
    reset(); voice(a1,a2); samples(warmup);
    if(release) spu_write16(0x18c,1);
    for(unsigned i=0;i<n;i++) {
        samples(1);
        uint16_t v=spu_read16(12);
        uint8_t le[2]={(uint8_t)v,(uint8_t)(v>>8)};
        Sha256_Update(&sha,le,2);
        if(!strcmp(name,"attack_linear")) CHECK(v==7*(i+1));
    }
    hash(name,expected);
}
static void envelopes(void)
{
    envelope("attack_linear",0x2c0f,0x1fc0,0,0,32,"88d2e4e5efa384a00cc5514ce5e549453c4c11212df70fb2fe4826b5beed7dc7");
    envelope("attack_exp",0xa40f,0x1fc0,0,0,1200,"2799ac8ecee2326befb72d2a943fb71bde42c7c6b7e77263a190c82d1e52df0c");
    envelope("decay",0,0x1fc0,0,0,16,"11c26d5da76917ae00adea6204fbdc414d2adc063e14a501e89becee8e15df8c");
    envelope("sustain",0x000f,0x4b0b,0,0,32,"7881a14e42acb5e6b57da3dd5f9900d8d93dfb990aea71cb1626f96a3903458f");
    envelope("release",0x000f,0x1fcb,4,1,2100,"d1d55c89ac80217b262affd1dccd619057f99d8dff6cc9546f3561b715b0a750");
    envelope("release_exp",0x000f,0x1feb,4,1,128,"1111218459122daaa3c6052c5ebda6ed14541aa70df42ce88cbc30e1927ccab0");
}
static void adpcm(void)
{
    reset();
    for(unsigned f=0;f<5;f++) for(unsigned s=0;s<13;s++) {
        uint8_t *b=spu_ram+0x1000+(f*13+s)*16;
        b[0]=(f<<4)|s; b[1]=0;
        for(unsigned j=0;j<14;j++) b[j+2]=j*37+19;
    }
    /* Hold sustain and integer pitch to isolate decode and interpolation. */
    voice(0xf,0x1fc0); samples(4); CHECK(spu_read16(12)==16383);
    spu_write16(12,32767); spu_write16(0,0x3fff); spu_write16(2,0x3fff);
    spu_write16(0x180,0x3fff); spu_write16(0x182,0x3fff); spu_write16(0x1aa,0xc000);
    spu_write16(4,0x1000); spu_set_output(collect,NULL); samples(65*28);
    CHECK(output_frames==65*28);
    hash("adpcm","5dbfa0325111adfba51ce30245e18788db5348d976a203c6b8fad7f9d81541c6");
    reset(); spu_ram[0x1001]=7; voice(0xf,0x1fc0); spu_write16(4,0x1000);
    samples(28); CHECK(spu_read16(0x19c)==1); CHECK(spu_read16(14)==0x200);
    CHECK(spu_read16(12)>0);
    spu_write16(0x188,1); CHECK(spu_read16(0x19c)==0);
    spu_write16(14,0x204); samples(28); CHECK(spu_read16(14)==0x204);
    reset(); spu_ram[0x1001]=1; voice(0xf,0x1fc0); spu_write16(4,0x1000);
    samples(28); CHECK(spu_read16(0x19c)==1); CHECK(spu_read16(12)==0);
}
static void transfers(void)
{
    reset(); spu_write16(0x1a6,0x200); spu_write16(0x1a8,0xabcd);
    CHECK(spu_ram[0x1000]==0xcd && spu_ram[0x1001]==0xab);
    uint32_t src[]={0x12345678,0x90abcdef}, dst[2]={0};
    spu_write16(0x1a6,0x202); spu_dma_write(src,2); CHECK(!spu_transfer_completed()); CHECK(spu_read16(0x1ae)&0x400);
    CHECK(spu_ram[0x1010]==0x78 && spu_ram[0x1017]==0x90);
    clock_advance(31); CHECK(!spu_transfer_completed()); clock_advance(1); CHECK(spu_transfer_completed());
    spu_write16(0x1a6,0x202); spu_dma_read(dst,2); CHECK(!memcmp(src,dst,sizeof(src)));
    clock_advance(32); CHECK(spu_transfer_completed());
    reset();
    spu_dma_write(src,2); clock_advance(8);
    spu_dma_write(src,1); spu_dma_read(dst,1);
    clock_advance(24); CHECK(!spu_transfer_completed()); /* first DMA deadline */
    clock_advance(16); CHECK(!spu_transfer_completed()); /* second DMA deadline */
    clock_advance(15); CHECK(!spu_transfer_completed());
    clock_advance(1); CHECK(spu_transfer_completed()); /* all 4 words */
    clock_advance(10); spu_dma_write(src,1);
    clock_advance(15); CHECK(!spu_transfer_completed());
    clock_advance(1); CHECK(spu_transfer_completed()); /* idle starts at now */
}
static void timing(void)
{
    reset(); voice(0x2c0f,0x1fc0); clock_advance(768*3+767);
    spu_write16(8,0x2d0f); CHECK(spu_read16(12)==21); CHECK(spu_samples()==3);
    samples(1); CHECK(spu_read16(12)==27);
    reset(); uint64_t target=clock_vblank_cycle(35964);
    clock_advance(target); CHECK(spu_samples()==26459973); CHECK(spu_samples()==target/768);
    reset();
    /* Split the same interval at non-sample and non-VBlank boundaries. */
    while(clock_now()<target) {
        uint64_t n=123456789;
        if(n>target-clock_now()) n=target-clock_now();
        clock_advance(n); spu_read16(0x1ae);
    }
    CHECK(spu_samples()==26459973);
    /* Cross signed-int rollover without rendering billions of silent frames. */
    reset(); samples((1u << 31) + 4); spu_set_output(collect,NULL); samples(64);
    CHECK(last[0]==0 && last[1]==0);
}

static void pitched_vectors(void)
{
    for (unsigned two=0;two<2;two++) for (unsigned mask=0;mask<(two?4u:2u);mask++) {
        reset();
        for(unsigned b=0;b<6;b++) {
            uint8_t *p=spu_ram+0x1000+b*16;
            p[0]=((b%2*2)<<4)|(2+b%2);
            for(unsigned j=0;j<14;j++) p[j+2]=j*37+19+b*11;
        }
        voice(0xf,0x1fc0);
        if(two) {
            memset(spu_ram+0x1802,0x77,14);
            spu_write16(6,0x300);spu_write16(0x188,1);
            spu_write16(4,0x1000);
            spu_write16(0x16,0x200);spu_write16(0x18,0xf);spu_write16(0x1a,0x1fc0);
            spu_write16(0x188,2);
        }
        samples(4); /* enter frozen sustain; voice 1 remains at counter zero */
        if(two) { spu_write16(4,0);spu_write16(12,18799); }
        unsigned base=two?16:0;
        spu_write16(base+12,32767);spu_write16(base,0x3fff);spu_write16(base+2,0x3fff);
        spu_write16(base+4,two?0x1000:0x1800);
        spu_write16(0x190,mask);
        spu_write16(0x180,0x3fff);spu_write16(0x182,0x3fff);spu_write16(0x1aa,0xc000);
        spu_set_output(collect,NULL);samples(84);
        /* Bit 0 has no effect. Muted voice 0 still modulates voice 1 pre-volume. */
        hash(two?"pmon":"fractional",two && !(mask&2)
             ? "388b2ece350eb00f3bf518294a17d9e38df2955e1de531851e87e7fb4f86edba"
             : "b2f80f8be739ad07355440bb0a552fb4cd0793b23ec34a276a9d9f69d9f98413");
    }
}
static void cd(int16_t *l,int16_t *r,void *arg) { (void)arg; *l=12000;*r=-8000; }
static void cd_and_reverb(void)
{
    reset(); spu_set_cd_input(cd,NULL);
    spu_write16(0x1aa,0xc001);spu_write16(0x1b0,0x4000);spu_write16(0x1b2,0x4000);
    spu_write16(0x180,0x3fff);spu_write16(0x182,0x3fff);
    spu_set_output(collect,NULL); samples(1);
    CHECK(last[0]==5999 && last[1]==-4000);
    spu_set_cd_input(NULL,NULL);samples(1);CHECK(last[0]==0 && last[1]==0);
    reset();
    /* Read-only APF2=0 impulse from synthetic RAM, then FIR -> EVOL. */
    spu_write16(0x1a2,0xff00); spu_write16(0x1aa,0xc000);
    spu_write16(0x184,0x7fff);spu_write16(0x186,0x7fff);
    spu_ram[0x7f800]=0x10;spu_ram[0x7f801]=0x27;
    spu_set_output(collect,NULL);samples(64);
    hash("reverb","832b1bb6a59a5973c4eaaac0120c7dccc850e9b789f0d957e73ea1be4e7f6131");
}
static void impulse(int16_t *l,int16_t *r,void *arg)
{
    unsigned *index=arg;
    *l=*r=(*index)++==0 ? 12000 : 0;
}
static void room(void)
{
    reset();psyq_libspu_reset();
    SpuReverbAttr attr={.mask=1,.mode=1};
    CHECK(SpuSetReverbModeParam(&attr)==0);
    for(unsigned i=0;i<0x26c0/2;i++) {
        int16_t v=(int16_t)((i*97+123)%65536-32768);
        spu_ram[0x7d940+i*2]=(uint8_t)v;
        spu_ram[0x7d941+i*2]=(uint16_t)v>>8;
    }
    memset(spu_ram,0x55,0x7d940);
    spu_write16(0x184,0x7fff);spu_write16(0x186,0x7fff);
    spu_write16(0x1b0,0x7fff);spu_write16(0x1b2,0x7fff);
    spu_write16(0x1aa,0xc085);
    unsigned index=0;spu_set_cd_input(impulse,&index);spu_set_output(collect,NULL);
    samples(1024);
    hash("ROOM","82d3ec374f9c2a4e1fbe554193c51686a219109a975982b8a0de5ebcc89bbbff");
    CHECK(index==1024);
    for(unsigned i=0;i<0x7d940;i++) CHECK(spu_ram[i]==0x55);
}
static void voice_reverb(void)
{
    for(unsigned on=0;on<2;on++) {
        reset();psyq_libspu_reset();
        spu_ram[0x1002]=7;voice(0xf,0x1fc0);samples(4);
        SpuReverbAttr attr={.mask=1,.mode=1};
        CHECK(SpuSetReverbModeParam(&attr)==0);
        spu_write16(12,32767);spu_write16(4,0x1000);
        spu_write16(0,0x3fff);spu_write16(2,0x3fff);
        spu_write16(0x180,0x3fff);spu_write16(0x182,0x3fff);
        spu_write16(0x184,0x7fff);spu_write16(0x186,0x7fff);
        spu_write16(0x198,on);spu_write16(0x1aa,0xc080);
        spu_set_output(collect,NULL);samples(12000);
        CHECK(on ? tail_nonzero>0 : tail_nonzero==0);
        hash(on?"eon_on":"eon_off",on
             ? "c892752005942249ec779f586362680d561a6b739b5eac484aa724ed39110145"
             : "e9352602e122549bceed8d5fe36927115afa988106e088cae16b0c84a0be7661");
    }
}
static void reset_fast_forward(void)
{
    uint8_t digests[2][32];
    for(int slow=0;slow<2;slow++) {
        reset();if(slow) spu_set_output(collect,NULL);
        samples(123457); /* normal rendering vs exact reset-state fast-forward */
        Sha256_Init(&sha);spu_set_output(collect,NULL);
        voice(0xf,0x1fc0);spu_write16(0x194,1);spu_write16(0x1aa,0xfc00);
        spu_write16(0,0x3fff);spu_write16(2,0x3fff);
        spu_write16(0x180,0x3fff);spu_write16(0x182,0x3fff);
        samples(1024);Sha256_Final(&sha,digests[slow]);
    }
    CHECK(!memcmp(digests[0],digests[1],32));
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void capture(void)
{
    reset();
    FILE *trace=tmpfile(), *wav=tmpfile(); CHECK(trace && wav);
    CHECK(spu_capture_begin(NULL,NULL)==-1);
    CHECK(spu_capture_begin(wav,wav)==-1);
    CHECK(spu_capture_end()==-1);
    /* Capture begins between sample boundaries, after draining earlier audio. */
    clock_advance(768*17+700);
    CHECK(spu_capture_begin(trace,wav)==0);
    CHECK(spu_capture_begin(trace,NULL)==-1);
    spu_set_output(collect,NULL);
    clock_advance(100);spu_write16(0x180,0x1234);
    samples(1024);
    CHECK(spu_capture_end()==0);
    CHECK(output_frames==1025);
    CHECK(ftell(wav)==44);
    rewind(wav); unsigned char header[44];
    CHECK(fread(header,1,44,wav)==44);
    CHECK(!memcmp(header,"RIFF",4) && !memcmp(header+8,"WAVEfmt ",8));
    CHECK(le32(header+4)==36+1025*4 && le32(header+40)==1025*4);
    CHECK(le32(header+24)==44100 && header[20]==1 && header[22]==2 && header[34]==16);
    for(unsigned i=0;i<1025*4;i++) CHECK(fgetc(wav)==0);
    CHECK(fgetc(wav)==EOF);
    rewind(trace);char line[128];
    CHECK(fgets(line,sizeof(line),trace) && !strcmp(line,"# dw1-spu-trace-v1\n"));
    CHECK(fgets(line,sizeof(line),trace) && !strcmp(line,"0 0 180 1234\n"));
    CHECK(!fgets(line,sizeof(line),trace));
    fclose(trace);fclose(wav);
    /* Trace-only capture and the VBlank origin are independent of PCM sampling. */
    reset();trace=tmpfile();CHECK(trace);
    clock_advance(clock_vblank_cycle(2)-clock_now());
    CHECK(spu_capture_begin(trace,NULL)==0);
    clock_advance(clock_vblank_cycle(4)-clock_now());spu_write16(4,0x1000);
    CHECK(spu_capture_end()==0);rewind(trace);
    CHECK(fgets(line,sizeof(line),trace));
    unsigned vb, reg, value;unsigned long long sample;
    CHECK(fscanf(trace,"%u %llu %x %x",&vb,&sample,&reg,&value)==4);
    CHECK(vb==2 && sample==(clock_vblank_cycle(4)-clock_vblank_cycle(2))/768 && reg==4 && value==0x1000);
    fclose(trace);
    /* Reusing a nonempty WAV must not overwrite evidence. */
    wav=tmpfile();CHECK(wav);CHECK(fputc(1,wav)==1);
    CHECK(spu_capture_begin(NULL,wav)==-1);fclose(wav);
    /* Reset finalizes the capture while the clock is still running. */
    wav=tmpfile();CHECK(wav);CHECK(spu_capture_begin(NULL,wav)==0);
    samples(1);spu_reset();rewind(wav);
    CHECK(fread(header,1,44,wav)==44 && le32(header+40)==4);
    CHECK(spu_capture_end()==-1);fclose(wav);
    /* A clock reset must finalize existing samples without attempting rewind. */
    wav=tmpfile();CHECK(wav);CHECK(spu_capture_begin(NULL,wav)==0);
    samples(2);clock_reset();CHECK(spu_capture_end()==-1);spu_reset();rewind(wav);
    CHECK(fread(header,1,44,wav)==44 && le32(header+40)==8);fclose(wav);
#ifndef __EMSCRIPTEN__
    /* Buffered write failures must be visible at end, rather than accepted. */
    trace=fopen("/dev/full","w");CHECK(trace);
    CHECK(spu_capture_begin(trace,NULL)==0);
    spu_write16(4,0);CHECK(spu_capture_end()==-1);fclose(trace);
#endif
}
/* A varying CD signal exposes byte shifts/duplication hidden by silent PCM. */
static void capture_signal(int16_t *left, int16_t *right, void *arg)
{
    unsigned *index = arg;
    *left = (int16_t)((*index * 73u) % 24001u - 12000);
    *right = (int16_t)((*index * 137u) % 16001u - 8000);
    ++*index;
}
static void capture_note_pcm(void)
{
    FILE *wav[2]; unsigned char hashes[2][32];
    for (unsigned anchored = 0; anchored < 2; anchored++) {
        reset();
        unsigned index = 0;
        spu_set_cd_input(capture_signal, &index);
        spu_write16(0x1aa, 0xc001);
        spu_write16(0x1b0, 0x4000); spu_write16(0x1b2, 0x4000);
        spu_write16(0x180, 0x3fff); spu_write16(0x182, 0x3fff);
        clock_advance(17 * 768 + 700);
        FILE *trace = tmpfile(); wav[anchored] = tmpfile(); CHECK(trace && wav[anchored]);
        CHECK(spu_capture_begin(trace, wav[anchored]) == 0);
        if (anchored) spu_capture_note_anchor();
        spu_set_output(collect, NULL);
        samples(1025); /* 4100 skipped bytes cross the 4096-byte copy block. */
        uint64_t origin = clock_now();
        spu_write16(0, 0x1234); /* The bundle starts before its confirming KON. */
        samples(3077); /* Multiple copy blocks must survive confirmation. */
        spu_write16(6, 0x200);
        spu_write16(0x188, 1); spu_write16(0x18a, 0);
        uint64_t cycle; uint32_t vblank;
        CHECK(spu_capture_origin(&cycle, &vblank) == 0);
        if (anchored) CHECK(cycle == origin);
        samples(2051); /* PCM produced after rebasing must also stay exact. */
        CHECK(spu_capture_end() == 0);
        CHECK(output_frames == 6153 && tail_nonzero > 10000);
        Sha256_Final(&sha, hashes[anchored]);
        fclose(trace);
        spu_set_cd_input(NULL, NULL);
    }
    /* Trimming evidence must not alter the original output callback's PCM. */
    CHECK(!memcmp(hashes[0], hashes[1], 32));
    CHECK(fseek(wav[0], 44 + 1025 * 4, SEEK_SET) == 0);
    rewind(wav[1]); unsigned char header[44];
    CHECK(fread(header, 1, 44, wav[1]) == 44 && le32(header + 40) == (6153 - 1025) * 4);
    for (unsigned i = 0; i < (6153 - 1025) * 4; i++) {
        int expected = fgetc(wav[0]); CHECK(expected != EOF && fgetc(wav[1]) == expected);
    }
    CHECK(fgetc(wav[0]) == EOF && fgetc(wav[1]) == EOF);
    fclose(wav[0]); fclose(wav[1]);
    spu_set_cd_input(NULL, NULL);
}
static void noise_and_voice24(void)
{
    reset();spu_write16(0x1aa,0xfc00);spu_write16(0x194,1);
    voice(0xf,0x1fc0);samples(4);spu_write16(12,32767);
    spu_write16(0,0x3fff);spu_write16(2,0x3fff);
    spu_write16(0x180,0x3fff);spu_write16(0x182,0x3fff);
    spu_set_output(collect,NULL);samples(128);
    hash("noise","15b471ee15ba0230f51f6a884acb9b05f71928b1f6879288b78b424f19bddd12");
    reset();spu_ram[0x1001]=1;
    spu_write16(23*16+6,0x200);spu_write16(23*16+4,0x1000);
    spu_write16(23*16+8,0xf);spu_write16(23*16+10,0x1fc0);
    spu_write16(0x18a,0x80);samples(4);
    CHECK(spu_read16(23*16+12)==16383);CHECK(spu_keys()==0x800000);
    samples(24);CHECK(spu_read16(0x19e)==0x80);CHECK(spu_read16(0x19c)==0);
#if !defined(__EMSCRIPTEN__) && !defined(NDEBUG)
    pid_t child=fork();CHECK(child>=0);
    if(!child) {
        setrlimit(RLIMIT_CORE,&(struct rlimit){0,0});freopen("/dev/null","w",stderr);
        uint32_t data[4]={0};spu_write16(0x1a6,0xffff);spu_dma_write(data,4);_exit(0);
    }
    int status;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status)==SIGABRT);
#endif
}
int main(void)
{
    transfers();envelopes();adpcm();pitched_vectors();cd_and_reverb();room();voice_reverb();reset_fast_forward();noise_and_voice24();timing();capture();capture_note_pcm();
    puts("SPU L1: OK");return 0;
}
