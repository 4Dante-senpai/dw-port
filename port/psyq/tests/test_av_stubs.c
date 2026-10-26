/* L1: audio API regression, absent cards, and the real USA movie TU over a synthetic Pack. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#define LANGUAGE_C
#include <kernel.h>
#include <libgpu.h>
#include <dw/mov.h>
#include "../psyq.h"
#include "../clock.h"
#include "../libpress.h"
#include "../../backend/cdrom.h"
#include "../../backend/gpu.h"
#include "../../backend/spu.h"
#include "../../host/pack/sha256.h"

/* The matching mw_lib41 assertions live in psyq_prototypes.c. */
_Static_assert(sizeof(StHEADER) == 32 && offsetof(StHEADER, frameCount) == 8 &&
               offsetof(StHEADER, width) == 16 && offsetof(StHEADER, loc) == 28,
               "port stream header ABI");
_Static_assert(sizeof(SpuReverbAttr) == 20 && offsetof(SpuReverbAttr, depth) == 8,
               "port SPU reverb ABI");

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL av:%d %s\n", __LINE__, #c); failures++; } } while (0)
#define ENTRY(call) do { uint64_t before = clock_now(); call; \
    CHECK(clock_now() - before == CLOCK_PSYQ_ENTRY_CYCLES); } while (0)

static void audio(void)
{
    clock_reset();
    psyq_libspu_reset();
    SsInit(); SsStart();
    ENTRY(SsSetTableSize(NULL, 1, 1)); SsSetTickMode(240); SsSetMVol(127, 127);
    for (short id = 0; id < 4; id++) {
        uint64_t close_start = clock_now();
        SsVabClose(id);
        CHECK(clock_now() - close_start == 41); /* Redux unused-bank path. */
        ENTRY(CHECK(SsVabOpenHeadSticky(NULL, id, 0x1000) == -1));
        ENTRY(CHECK(SsVabTransBody(NULL, id) == -1));
        CHECK(SsVabTransCompleted(0) == 1);
        CHECK(SsVabTransCompleted(1) == 1);
        uint64_t lookup_start = clock_now();
        CHECK(SsUtGetVBaddrInSB(id) == 0xfffffffful);
        /* Redux vhb-tail-probe: the lookup takes 40 cycles, including empty banks. */
        CHECK(clock_now() - lookup_start == 40);
    }
    ENTRY(CHECK(SsSeqOpen(NULL, 2) == -1));
    ENTRY(SsSeqSetVol(0, 80, 80)); ENTRY(SsSeqPlay(0, 1, 0));
    ENTRY(CHECK(SsUtKeyOnV(10, 0, 1, 0, 60, 0, 127, 127) == -1));
    ENTRY(CHECK(SpuGetKeyStatus(1 << 10) == SPU_OFF));
    ENTRY(CHECK(SsUtAutoVol(10, 127, 0, 6) == 0));
    ENTRY(CHECK(SsUtKeyOffV(10) == 0)); SsUtAllKeyOff(0);
    ENTRY(CHECK(SpuSetReverb(1) == 1));
    SpuReverbAttr attr = {.mask = 2, .depth = {.left = 0x1000}};
    ENTRY(CHECK(SpuSetReverbDepth(&attr) == 0));
    attr.mask = 1;
    attr.mode = 3;
    ENTRY(CHECK(SpuSetReverbModeParam(&attr) == 0));
    uint64_t before_clear = clock_now();
    CHECK(SpuClearReverbWorkArea(3) == 0);
    CHECK(clock_now() - before_clear == 117167); /* Redux: 19 DMA/WaitEvent chunks. */
    ENTRY(CHECK(SpuSetReverbVoice(1, 0x7fffff) == 0x7fffff));
    ENTRY(SsSeqStop(0)); ENTRY(SsSeqClose(0)); SsEnd(); SsQuit();
}

static void cards(void)
{
    unsigned long command = 99, result = 99;
    clock_reset(); ResetCallback(); MemCardInit(0); MemCardStart();
    CHECK(MemCardSync(1, &command, &result) == -1);
    CHECK(command == 99 && result == 99);
    CHECK(MemCardExist(0) == 1);
    CHECK(MemCardAccept(0) == 0);
    CHECK(MemCardSync(0, &command, &result) == 1);
    CHECK(command == McFuncExist && result == McErrCardNotExist);
    MemCardStop();
}

static unsigned long output[3][10];
static int callbacks, depth, max_depth;
static pthread_t game_thread;
static void decoded(void)
{
    depth++;
    if (depth > max_depth) max_depth = depth;
    CHECK(pthread_equal(game_thread, pthread_self()));
    CHECK(callbacks < 3);
    if (callbacks >= 3) { depth--; return; }
    for (int i = 1; i < 9; i++) CHECK(output[callbacks][i] == 0);
    CHECK(output[callbacks][0] == 0xDEADBEEF && output[callbacks][9] == 0xDEADBEEF);
    callbacks++;
    if (callbacks < 3) {
        DecDCTout(&output[callbacks][1], 8);
        /* Nested call returns before its buffer is filled or its callback runs. */
        CHECK(output[callbacks][1] == 0xDEADBEEF);
    }
    depth--;
}

static int reset_callbacks;
static unsigned long canceled_output[8];
static void count_reset_callback(void)
{
    reset_callbacks++;
}

static void reset_with_output_pending(void)
{
    reset_callbacks++;
    if (reset_callbacks == 1) {
        DecDCTout(canceled_output, 8);
        DecDCTReset(0);
    }
}

static void streaming(void)
{
    unsigned long *bs, *head, *bs2, *head2;
    unsigned long runbuf[128] = {0}, *run = runbuf;
    CdlLOC loc;
    clock_reset();
    spu_reset(); psyq_libcd_reset();
    static unsigned long ring[24*2048/4];
    ENTRY(StSetRing(ring, 24)); ENTRY(StSetStream(1, 1, (unsigned long)-1, NULL, NULL));
    /* Use a valid physical LBA even with no Pack attached. The reset BCD
     * location 00:00:00 is in the lead-in, not an unsigned seek distance. */
    CdIntToPos(0, &loc); CHECK(CdControl(CdlSetloc, (uint8_t *)&loc, NULL));
    uint64_t read_start = clock_now();
    CHECK(CdRead2(0x1e0) == 1);
    CHECK(clock_now() - read_start == CLOCK_PSYQ_ENTRY_CYCLES +
          3500 + 33868800ull*797/1000000);
    clock_advance(33868800ull*797/1000000 + 33868800ull*20000/1000000 +
                  33868800ull*1000/1000000 + 33868800ull*650000/1000000 +
                  33868800ull*6666/1000000);
    uint64_t poll_start = clock_now();
    CHECK(StGetNext(&bs, &head) == 0);
    CHECK(clock_now() - poll_start == 38);
    poll_start = clock_now();
    CHECK(StGetNext(&bs2, &head2) == 0);
    CHECK(clock_now() - poll_start == 38);
    CHECK(bs == bs2 && head == head2);
    StHEADER *h = (StHEADER *)head;
    CHECK(h->frameCount == 0x7FFFFFFFUL && h->width == 320 && h->height == 240);
    ENTRY(CHECK(StFreeRing(bs) == 1));
    ENTRY(CHECK(StGetBackloc(&loc) == 0));
    CHECK(CdPosToInt(&loc) == -149);
    ENTRY(StCdInterrupt()); ENTRY(StUnSetRing());
    DecDCTReset(0);
    uint64_t vlc_start = clock_now();
    CHECK(DecDCTvlc2(bs, run, MOV_VLC_TABLE) == 0);
    /* Empty v2 frame: 29 setup + 5 terminal + 269 padding instructions,
     * with 9 setup and 65 padding RAM accesses in the Redux interpreter. */
    CHECK(clock_now() - vlc_start == (29+5+269)*2 + 9+65);
    CHECK(run[0] == 0x38000000);
    ENTRY(DecDCTin(run, 3));
    for (int n = 0; n < 3; n++)
        for (int i = 0; i < 10; i++) output[n][i] = 0xDEADBEEF;
    game_thread = pthread_self();
    ENTRY(CHECK(DecDCToutCallback(decoded) == 0));
    uint64_t before = clock_now();
    DecDCTout(&output[0][1], 8);
    CHECK(clock_now() - before == CLOCK_PSYQ_ENTRY_CYCLES);
    CHECK(callbacks == 0);
    clock_advance(100000);
    CHECK(callbacks == 3 && max_depth == 1 && depth == 0);
    DecDCToutCallback(NULL);
    ENTRY(DecDCTout(run, 1)); clock_advance(100000); CHECK(run[0] == 0 && callbacks == 3);

    ENTRY(CHECK(DecDCToutCallback(count_reset_callback) == 0));
    DecDCTReset(0);
    run[0] = 9;
    ENTRY(DecDCTout(run, 1)); clock_advance(100000); CHECK(run[0] == 0 && reset_callbacks == 0);

    for (int i = 0; i < 8; i++) canceled_output[i] = 0xDEADBEEF;
    ENTRY(CHECK(DecDCToutCallback(reset_with_output_pending) == 0));
    run[0] = 9;
    DecDCTout(run, 1);
    clock_advance(100000); CHECK(run[0] == 0 && reset_callbacks == 1);
    for (int i = 0; i < 8; i++) CHECK(canceled_output[i] == 0xDEADBEEF);
}

static int movie_active, slices;
int __real_LoadImage(RECT *rect, unsigned long *src);
int __wrap_LoadImage(RECT *rect, unsigned long *src)
{
    if (movie_active) {
        CHECK(rect->x == slices * 24 && rect->y == 32 && rect->w == 24 && rect->h == 240);
        CHECK(src == ((slices & 1) ? MOV_SLICE_BUFFER_1 : MOV_SLICE_BUFFER_0));
        for (int i = 0; i < 2880; i++) CHECK(src[i] == 0);
        CHECK(MOV_ENV.flipPending == (slices == 19));
        slices++;
    }
    return __real_LoadImage(rect, src);
}

static void movie(void)
{
    /* Reuse test_pack's fixture: it already contains MOVIE/OP1.STR. */
    FILE *f = fopen("pack_t.tsv", "rb");
    CHECK(f != NULL); if (!f) exit(1);
    Sha256 sha; uint8_t hash[32], bytes[1024]; size_t n; char hex[65], err[512];
    Sha256_Init(&sha);
    while ((n = fread(bytes, 1, sizeof(bytes), f))) Sha256_Update(&sha, bytes, n);
    CHECK(!ferror(f)); fclose(f); Sha256_Final(&sha, hash);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
    CHECK(cdrom_open("pack_t1.pack", hex, err, sizeof(err)) == 0);
    clock_reset(); spu_reset(); psyq_libetc_reset(); psyq_libcd_reset();
    ResetGraph(0); CdInit();
    gpu_fill(0, 0, 1024, 512, 0xFFFFFF);
    memset(MOV_SLICE_BUFFER_0, 0xA5, sizeof(MOV_SLICE_BUFFER_0));
    memset(MOV_SLICE_BUFFER_1, 0xA5, sizeof(MOV_SLICE_BUFFER_1));
    MOVIE_WIDTH = MOVIE_HEIGHT = MOVIE_COMPLETED = 0;
    uint32_t before = clock_vblanks();
    movie_active = 1;
    CHECK(MOV_playMovie(0) == 1);
    movie_active = 0;
    CHECK(MOVIE_COMPLETED == 1 && MOVIE_WIDTH == 320 && MOVIE_HEIGHT == 240);
    CHECK(slices == 20 && MOV_ENV.frameIndex == 1 && MOV_ENV.flipPending == 0);
    printf("movie: slices=%d vblanks=%u\n", slices, clock_vblanks() - before);
    /* Includes the controller's 650 ms speed change, seek, directory fallback,
     * VLC CPU work and blocking 2x Pause after detaching the STR ring. */
    CHECK(clock_vblanks() - before == 52); /* Includes the measured ResetGraph CPU phase. */
    const uint16_t *vram = gpu_vram();
    for (int y = 32; y < 272; y++)
        for (int x = 0; x < 480; x++) CHECK(vram[y * 1024 + x] == 0);
    CHECK(vram[500 * 1024 + 500] != 0); /* Outside the frame remains untouched. */
    /* Verify callback removal; otherwise this would submit another movie slice. */
    unsigned long word = 1; DecDCTout(&word, 1); clock_advance(100000); CHECK(word == 0 && slices == 20);
    cdrom_close();
}

int main(void)
{
    const char *names[] = {"mdec_iq", "mdec_coef"};
    for (unsigned i=0;i<2;i++) {
        size_t n; libpress_table(names[i],&n); uint32_t *data=calloc(1,n);
        CHECK(data); data[0]=i?0x60000000:0x40000001;
        CHECK(!libpress_set_table(names[i],data,n)); free(data);
    }
    /* Synthetic compressed VLC table: a literal 0x11000-byte zero table. */
    size_t n; libpress_table("data_2",&n); uint8_t *data=calloc(1,n); CHECK(data);
    size_t at=0; data[at++]=0; data[at++]=0; data[at++]=0xf1; data[at++]=0;
    unsigned remaining=0x11000-1;
    while(remaining) { unsigned count=remaining>240?240:remaining; data[at++]=count-1; remaining-=count; }
    data[at++]=0xff; data[at++]=0xff; CHECK(at<=n);
    CHECK(!libpress_set_table("data_2",data,n)); free(data);
    audio(); cards(); streaming(); movie();
    printf("av_stubs: %s\n", failures ? "FAIL" : "OK");
    return failures != 0;
}
