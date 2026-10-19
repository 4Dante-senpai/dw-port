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
#include "../../backend/cdrom.h"
#include "../../backend/gpu.h"
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
    ENTRY(SsInit()); ENTRY(SsStart());
    ENTRY(SsSetTableSize(NULL, 1, 1)); ENTRY(SsSetTickMode(240)); SsSetMVol(127, 127);
    for (short id = 0; id < 4; id++) {
        ENTRY(SsVabClose(id));
        ENTRY(CHECK(SsVabOpenHeadSticky(NULL, id, 0x1000) == -1));
        ENTRY(CHECK(SsVabTransBody(NULL, id) == -1));
        CHECK(SsVabTransCompleted(0) == 1);
        CHECK(SsVabTransCompleted(1) == 1);
        ENTRY(CHECK(SsUtGetVBaddrInSB(id) == 0xfffffffful));
    }
    ENTRY(CHECK(SsSeqOpen(NULL, 2) == 0));
    ENTRY(SsSeqSetVol(0, 80, 80)); ENTRY(SsSeqPlay(0, 1, 0));
    ENTRY(CHECK(SsUtKeyOnV(10, 0, 1, 0, 60, 0, 127, 127) == 0));
    ENTRY(CHECK(SpuGetKeyStatus(1 << 10) == SPU_OFF));
    ENTRY(CHECK(SsUtAutoVol(10, 127, 0, 6) == 0));
    ENTRY(CHECK(SsUtKeyOffV(10) == 0)); ENTRY(SsUtAllKeyOff(0));
    ENTRY(CHECK(SpuSetReverb(1) == 1));
    SpuReverbAttr attr = {.mask = 2, .depth = {.left = 0x1000}};
    ENTRY(CHECK(SpuSetReverbDepth(&attr) == 0));
    attr.mask = 1;
    attr.mode = 3;
    ENTRY(CHECK(SpuSetReverbModeParam(&attr) == 0));
    uint64_t before_clear = clock_now();
    CHECK(SpuClearReverbWorkArea(3) == 0);
    CHECK(clock_now() - before_clear == CLOCK_PSYQ_ENTRY_CYCLES + (0x80000 - 0xf6f8 * 8) * 4);
    ENTRY(CHECK(SpuSetReverbVoice(1, 0x7fffff) == 0x7fffff));
    ENTRY(SsSeqStop(0)); ENTRY(SsSeqClose(0)); ENTRY(SsEnd()); ENTRY(SsQuit());
}

static void cards(void)
{
    unsigned long cmd = 99, result = 99;
    long data = 0x12345678, files = 23;
    struct DIRENTRY dir[15];
    clock_reset();
    ENTRY(MemCardInit(0)); ENTRY(MemCardStart());
    ENTRY(CHECK(MemCardSync(1, &cmd, &result) == -1));
    CHECK(cmd == 99 && result == 99);
    for (int mode = 0; mode <= 1; mode++) {
        for (int channel = 0; channel <= 0x10; channel += 0x10) {
            ENTRY(CHECK(MemCardExist(channel) == 1));
            ENTRY(CHECK(MemCardSync(mode, &cmd, &result) == 1));
            CHECK(cmd == McFuncExist && result == McErrCardNotExist);
            ENTRY(CHECK(MemCardSync(mode, &cmd, &result) == -1));
            ENTRY(CHECK(MemCardAccept(channel) == 1));
            ENTRY(CHECK(MemCardSync(mode, &cmd, &result) == 1));
            CHECK(cmd == McFuncAccept && result == McErrCardNotExist);
            ENTRY(CHECK(MemCardReadFile(channel, "save", &data, 0, 4) == 1));
            ENTRY(CHECK(MemCardSync(mode, &cmd, &result) == 1));
            CHECK(cmd == McFuncReadFile && result == McErrCardNotExist && data == 0x12345678);
            ENTRY(CHECK(MemCardWriteFile(channel, "save", &data, 0, 4) == 1));
            ENTRY(CHECK(MemCardSync(mode, &cmd, &result) == 1));
            CHECK(cmd == McFuncWriteFile && result == McErrCardNotExist && data == 0x12345678);
            ENTRY(CHECK(MemCardCreateFile(channel, "save", 1) == McErrCardNotExist));
            ENTRY(CHECK(MemCardDeleteFile(channel, "save") == McErrCardNotExist));
            ENTRY(CHECK(MemCardFormat(channel) == McErrCardNotExist));
            memset(dir, 0xA5, sizeof(dir));
            ENTRY(CHECK(MemCardGetDirentry(channel, "*", dir, &files, 0, 15) == McErrCardNotExist));
            for (size_t i = 0; i < sizeof(dir); i++) CHECK(((unsigned char *)dir)[i] == 0xA5);
            CHECK(files == 23);
        }
    }
    ENTRY(CHECK(MemCardExist(0) == 1));
    ENTRY(CHECK(MemCardSync(0, NULL, NULL) == 1));
    ENTRY(CHECK(MemCardSync(0, NULL, NULL) == -1));
    MemCardExist(0); MemCardInit(0);
    CHECK(MemCardSync(1, NULL, NULL) == -1);
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
    unsigned long *bs, *head, *bs2, *head2, run = 9;
    CdlLOC loc;
    clock_reset();
    ENTRY(CHECK(CdRead2(0) == 1));
    ENTRY(StSetRing(NULL, 24)); ENTRY(StSetStream(1, 1, (unsigned long)-1, NULL, NULL));
    ENTRY(CHECK(StGetNext(&bs, &head) == 0));
    ENTRY(CHECK(StGetNext(&bs2, &head2) == 0));
    CHECK(bs == bs2 && head == head2);
    StHEADER *h = (StHEADER *)head;
    CHECK(h->frameCount == 0xFFFFFFFFUL && h->width == 320 && h->height == 240);
    ENTRY(CHECK(StFreeRing(bs) == 0));
    ENTRY(CHECK(StGetBackloc(&loc) == 0));
    CHECK(loc.minute == 0 && loc.second == 0 && loc.sector == 0);
    ENTRY(StCdInterrupt()); ENTRY(StUnSetRing());
    ENTRY(DecDCTReset(0)); ENTRY(DecDCTvlcBuild(NULL));
    ENTRY(CHECK(DecDCTvlc2(bs, &run, NULL) == 0)); CHECK(run == 0);
    ENTRY(DecDCTin(&run, 3));
    for (int n = 0; n < 3; n++)
        for (int i = 0; i < 10; i++) output[n][i] = 0xDEADBEEF;
    game_thread = pthread_self();
    ENTRY(CHECK(DecDCToutCallback(decoded) == 0));
    uint64_t before = clock_now();
    DecDCTout(&output[0][1], 8);
    CHECK(clock_now() - before == 3 * CLOCK_PSYQ_ENTRY_CYCLES);
    CHECK(callbacks == 3 && max_depth == 1 && depth == 0);
    ENTRY(CHECK(DecDCToutCallback(NULL) == 0));
    ENTRY(DecDCTout(&run, 1)); CHECK(run == 0 && callbacks == 3);

    ENTRY(CHECK(DecDCToutCallback(count_reset_callback) == 0));
    ENTRY(DecDCTReset(0));
    run = 9;
    ENTRY(DecDCTout(&run, 1)); CHECK(run == 0 && reset_callbacks == 0);

    for (int i = 0; i < 8; i++) canceled_output[i] = 0xDEADBEEF;
    ENTRY(CHECK(DecDCToutCallback(reset_with_output_pending) == 0));
    run = 9;
    DecDCTout(&run, 1);
    CHECK(run == 0 && reset_callbacks == 1);
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
    clock_reset(); psyq_libetc_reset(); psyq_libcd_reset();
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
    CHECK(clock_vblanks() - before == 8); /* Fixed virtual timing, no host time. */
    const uint16_t *vram = gpu_vram();
    for (int y = 32; y < 272; y++)
        for (int x = 0; x < 480; x++) CHECK(vram[y * 1024 + x] == 0);
    CHECK(vram[500 * 1024 + 500] != 0); /* Outside the frame remains untouched. */
    /* Verify callback removal; otherwise this would submit another movie slice. */
    unsigned long word = 1; DecDCTout(&word, 1); CHECK(word == 0 && slices == 20);
    cdrom_close();
}

int main(void)
{
    audio(); cards(); streaming(); movie();
    printf("av_stubs: %s\n", failures ? "FAIL" : "OK");
    return failures != 0;
}
