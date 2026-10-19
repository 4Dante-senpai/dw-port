/* L1: synthetic writable VH/VB; literal expectations from the USA mw_lib41 asm.
 * No game disc or DW2-PC implementation is used as an oracle. */
#include "../psyq.h"
#include "../clock.h"
#include "../../backend/spu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1); } } while (0)

static unsigned char vh[0x20 + 128 * 16 + 512 + 512];
static unsigned char vb[192];
static uint32_t allocation_records[18];

static void put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static unsigned get16(const unsigned char *p) { return p[0] | (p[1] << 8); }

static void header(unsigned version, unsigned format)
{
    memset(vh, 0, sizeof(vh));
    put32(vh, 0x56414200 | format);
    put32(vh + 4, version);
    put16(vh + 0x12, 1);
    put16(vh + 0x16, 2);
    vh[0x20 + 3 * 16] = 1;
    unsigned char *sizes = vh + 0x20 + (format == 0x70 && version >= 5 ? 128 : 64) * 16 + 512;
    /* Sample 0: 0 bytes, sample 1: 16 bytes, sample 2: 64 bytes.
     * Version 4 units are four bytes, version 5 units are eight bytes. */
    put16(sizes + 2, version >= 5 ? 2 : 4);
    put16(sizes + 4, version >= 5 ? 8 : 16);
}

static void allocator(void)
{
    CHECK(SpuInitMalloc(0, NULL) == 0);
    CHECK(SpuMalloc(16) == -1);
    CHECK(SpuInitMalloc(8, (char *)allocation_records) == 8);
    CHECK(allocation_records[0] == 0x40001010 && allocation_records[1] == 0x7eff0);
    CHECK(SpuMalloc(9) == 0x1010);   /* 16-byte rounded allocation. */
    CHECK(SpuMalloc(24) == 0x1020);
    CHECK(SpuMalloc(32) == 0x1038);
    SpuFree(0x1020);
    CHECK(SpuMalloc(8) == 0x1020);   /* Splits a free record. */
    CHECK(SpuMalloc(16) == 0x1028);
    SpuFree(0x1020); SpuFree(0x1028); SpuFree(0x1038);
    CHECK(SpuMalloc(56) == 0x1020);  /* Coalesced holes and terminal region. */
    SpuFree(0x1020); SpuFree(0x1010); SpuFree(0x7770);
    CHECK(allocation_records[0] == 0x40001010 && allocation_records[1] == 0x7eff0);
    CHECK(SpuMalloc(0x7eff8) == -1);
    CHECK(SpuMalloc(0x7eff0) == 0x1010);
    CHECK(SpuMalloc(16) == -1);
    SpuFree(0x1010);
    CHECK(SpuMalloc(16) == 0x1010);
    SpuFree(0x1010);
    /* A live allocation overlapping reverb prevents clearing its data. */
    CHECK(SpuMalloc(0x7efe8) == 0x1010);
    CHECK(SpuClearReverbWorkArea(1) == -1);
    CHECK(SpuSetReverb(1) == 0);
    SpuReverbAttr room = {.mask = 1, .mode = 1};
    CHECK(SpuSetReverbModeParam(&room) == -1);
    SpuFree(0x1010);
    CHECK(SpuSetReverbModeParam(&room) == 0);
    CHECK(SpuSetReverb(1) == 1);
    CHECK(SpuSetReverb(0) == 0);
    CHECK(SpuMalloc(0) == -1 && SpuMalloc(7) == -1);
    /* num is the count of live records; the caller supplies one extra tail. */
    CHECK(SpuInitMalloc(2, (char *)allocation_records) == 2);
    CHECK(SpuMalloc(16) == 0x1010 && SpuMalloc(16) == 0x1020);
    CHECK(SpuMalloc(16) == -1);
    SpuFree(0x1010);
    CHECK(SpuMalloc(16) == 0x1010);
    SpuFree(0x1010); SpuFree(0x1020);
}

static void master_volume(void)
{
    SsSetMVol(127, 64);
    CHECK(spu_read16(0x180) == 16383 && spu_read16(0x182) == 8256);
    SpuCommonAttr a = {.mask = 1 | 4, .mvol = {-1, 321}, .mvolmode = {7, 1}};
    SpuSetCommonAttr(&a);
    CHECK(spu_read16(0x180) == 0xe000 && spu_read16(0x182) == 8256);
    a.mvol.left = 200; SpuSetCommonAttr(&a); CHECK(spu_read16(0x180) == 0xe07f);
    a.mask = 4; a.mvol.left = 123; SpuSetCommonAttr(&a); /* Mode alone does not write volume. */
    CHECK(spu_read16(0x180) == 0xe07f);
    a.mask = 1; a.mvol.left = -2; SpuSetCommonAttr(&a); CHECK(spu_read16(0x180) == 0x7ffe);
    a = (SpuCommonAttr){.mask = 0, .mvol = {10, 20}, .cd = {{30, 40}, 1, 0},
                        .ext = {{50, 60}, 0, 1}};
    spu_write16(0x1aa, 0xc080);
    SpuSetCommonAttr(&a);
    CHECK(spu_read16(0x180) == 10 && spu_read16(0x182) == 20);
    CHECK(spu_read16(0x1b0) == 30 && spu_read16(0x1b2) == 40);
    CHECK(spu_read16(0x1b4) == 50 && spu_read16(0x1b6) == 60);
    CHECK(spu_read16(0x1aa) == 0xc086);
}

static void transfer(void)
{
    CHECK(SpuSetTransferStartAddr(0x100f) == 0);
    CHECK(SpuSetTransferStartAddr(0x7fff9) == 0);
    CHECK(SpuSetTransferStartAddr(0x1011) == 0x1018);
    CHECK(SpuSetTransferMode(7) == 0);
    memset(spu_ram + 0x1018, 0xcc, 192);
    uint64_t start = clock_now();
    CHECK(SpuWrite(vb, 80) == 80);
    CHECK(memcmp(spu_ram + 0x1018, vb, 128) == 0); /* BCR transfers two blocks. */
    CHECK(spu_ram[0x1098] == 0xcc);
    CHECK(SpuIsTransferCompleted(0) == 0);
    CHECK(SpuIsTransferCompleted(1) == 1);
    CHECK(clock_now() == start + CLOCK_PSYQ_ENTRY_CYCLES + 128 * 4);
    CHECK((spu_read16(0x1aa) & 0x30) == 0);
    CHECK(SpuSetTransferMode(1) == 1);
    CHECK(SpuSetTransferStartAddr(0x2000) == 0x2000);
    CHECK(SpuWrite(vb, 80) == 80);
    CHECK(memcmp(spu_ram + 0x2000, vb, 80) == 0);
    CHECK(SpuIsTransferCompleted(0) == 1);
    /* A DMA beginning in the last eight bytes wraps at the end of sound RAM. */
    CHECK(SpuSetTransferMode(0) == 0);
    CHECK(SpuSetTransferStartAddr(0x7fff8) == 0x7fff8);
    CHECK(SpuWrite(vb, 64) == 64);
    CHECK(SpuIsTransferCompleted(1) == 1);
    CHECK(memcmp(spu_ram + 0x7fff8, vb, 8) == 0 && memcmp(spu_ram, vb + 8, 56) == 0);
}

static void banks(void)
{
    CHECK(SsVabOpenHeadSticky(NULL, 0, 0x3000) == -1);
    header(5, 0x70);
    CHECK(SsVabOpenHeadSticky(vh, -2, 0x3000) == -1);
    CHECK(SsVabOpenHeadSticky(vh, 16, 0x3000) == -1);
    CHECK(SsVabOpenHeadSticky(vh, -1, 0x3000) == 0);
    CHECK(SsUtGetVBaddrInSB(0) == 0xfffffffful); /* Header-only is not playable. */
    CHECK(get16(vh + 0x20 + 12) == 0x600);
    CHECK(get16(vh + 0x20 + 14) == 0x602);
    CHECK(get16(vh + 0x20 + 16 + 12) == 0x60a);
    CHECK(vh[0x20 + 3 * 16 + 8] == 0 && vh[0x20 + 4 * 16 + 8] == 1);
    CHECK(SsVabOpenHeadSticky(vh, 1, 0x4000) == -1); /* Reservation held. */
    SsVabClose(0); /* Closing a header-only bank leaves the reservation held. */
    CHECK(SsVabTransBody(vb, 0) == 0);
    CHECK(SsUtGetVBaddrInSB(0) == 0x3000);
    CHECK(SsVabOpenHeadSticky(vh, 1, 0x4000) == -1); /* DMA still pending. */
    CHECK(SsVabTransCompleted(0) == 0);
    CHECK(SsVabTransCompleted(1) == 1);
    CHECK(memcmp(spu_ram + 0x3000, vb, 128) == 0);
    CHECK(SsVabOpenHeadSticky(vh, 0, 0x4000) == -1); /* Occupied slot. */
    SsVabClose(0);
    CHECK(SsUtGetVBaddrInSB(0) == 0xfffffffful);
    CHECK(SsUtGetVBaddrInSB(-1) == 0xfffffffful && SsUtGetVBaddrInSB(16) == 0xfffffffful);
    for (int version = 4; version <= 5; version++) {
        for (unsigned format = 0x70; format <= 0x71; format++) {
            header(version, format);
            CHECK(SsVabOpenHeadSticky(vh, 3, 0x5000) == 3);
            CHECK(SsVabTransBody(vb, 3) == 3);
            CHECK(SsVabTransCompleted(1) == 1);
            CHECK(memcmp(spu_ram + 0x5000, vb, 128) == 0);
            SsVabClose(3);
        }
    }
    header(5, 0x70); vh[1] = 0; CHECK(SsVabOpenHeadSticky(vh, 0, 0x5000) == -1);
    header(5, 0x70); put16(vh + 0x12, 129); CHECK(SsVabOpenHeadSticky(vh, 0, 0x5000) == -1);
    header(5, 0x70); CHECK(SsVabOpenHeadSticky(vh, 0, 0x7fff8) == -1);
    CHECK(SsVabTransBody(vb, -1) == -1 && SsVabTransBody(vb, 16) == -1);
    CHECK(SsVabTransCompleted(0) == 1);
    /* Fill the 16 independent slots and exercise the automatic-id search. */
    for (short id = 0; id < 16; id++) {
        header(5, 0x70);
        CHECK(SsVabOpenHeadSticky(vh, -1, 0x6000 + id * 128) == id);
        CHECK(SsVabTransBody(vb, id) == id);
        CHECK(SsVabTransCompleted(1) == 1);
    }
    CHECK(SsVabOpenHeadSticky(vh, -1, 0x7000) == -1);
    for (short id = 0; id < 16; id++) SsVabClose(id);
    header(5, 0x70);
    CHECK(SsVabOpenHeadSticky(vh, -1, 0x6000) == 0);
    CHECK(SsVabTransBody(vb, 0) == 0 && SsVabTransCompleted(1) == 1);
    SsVabClose(0);
}

int main(void)
{
    clock_reset(); psyq_libspu_reset();
    for (unsigned i = 0; i < sizeof(vb); i++) vb[i] = i ^ 0x5a;
    allocator(); transfer(); banks(); master_volume();
    puts("libsnd L1 VAB/transfer/allocator/master volume: OK");
    return 0;
}
