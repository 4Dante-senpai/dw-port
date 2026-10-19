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
static uint32_t seq_table[0xb0 * 4 / 4];

static void put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(unsigned char *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
static unsigned get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static uint32_t get32(const unsigned char *p) { return get16(p) | ((uint32_t)get16(p + 2) << 16); }

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

static void sequence_records(void)
{
    unsigned char *table = (unsigned char *)seq_table;
    memset(table, 0xa5, sizeof(seq_table));
    SsSetTableSize((char *)table, 2, 2);
    for (unsigned record = 0; record < 4; record++) {
        unsigned char *q = table + record * 0xb0;
        CHECK(q[0x22] == 255 && q[0x23] == 0 && q[0x21] == 0xa5);
        CHECK(get32(q + 0x98) == 0 && get32(q + 0xac) == 0);
        CHECK(get16(q + 0x4e) == 0 && get16(q + 0x58) == 127 && get16(q + 0x5e) == 127);
        CHECK(q[0] == 0xa5 && q[0xaf] == 0); /* No whole-record memset. */
    }
    unsigned long seq_words[8] = {0};
    unsigned char *seq = (unsigned char *)seq_words;
    seq[0] = 'p'; seq[7] = 1;
    seq[8] = 1; seq[9] = 0xe0; /* 480 PPQN. */
    seq[10] = 7; seq[11] = 0xa1; seq[12] = 0x20; /* 500000 us => 120 BPM. */
    seq[13] = 4; seq[14] = 2;
    seq[15] = 0x81; seq[16] = 0; /* 128 MIDI ticks => 1280 internal units. */
    SsSetTickMode(2);
    CHECK(SsSeqOpen(seq_words, 3) == 0);
    CHECK(get16(table + 0x50) == 480 && get16(table + 0x52) == 0xffff);
    CHECK(get16(table + 0x54) == 40 && get16(table + 0x56) == 40);
    CHECK(get32(table + 0x84) == 1280 && get32(table + 0x88) == 1280);
    CHECK(get32(table + 0x8c) == 120 && get32(table + 0x94) == 120);
    CHECK(get32(table) == (uint32_t)(uintptr_t)(seq + 17));
    CHECK(table[0x26] == 3 && table[0x24] == 4 && table[0x25] == 2);
    for (int i = 0; i < 16; i++) {
        CHECK(table[0x37 + i] == i && table[0x27 + i] == 64);
        CHECK(get16(table + 0x60 + 2 * i) == 127);
    }
    SsSeqSetVol(0, 200, -1);
    CHECK(get16(table + 0x58) == 200 && get16(table + 0x5a) == 0xffff);
    SsSeqPlay(0, 1, 257);
    CHECK(get32(table + 0x98) == 1 && table[0x14] == 1 && table[0x20] == 1);
    CHECK(get16(table + 0x58) == 127 && get16(table + 0x5a) == 127);
    SsSeqSetVol(0, 80, 64);
    CHECK(get16(table + 0x58) == 80 && get16(table + 0x5a) == 64);
    SsSeqStop(0);
    CHECK(get32(table + 0x98) == 4 && table[0x14] == 0);
    CHECK(get32(table + 0x88) == 0 && get32(table + 0x90) == 1280);
    CHECK(get16(table + 0x58) == 80 && get16(table + 0x5c) == 127);
    SsSeqClose(0);
    CHECK(get32(table + 0x98) == 0 && get16(table + 0x58) == 127);
    seq[7] = 0;
    CHECK(SsSeqOpen(seq_words, 0) == -1); /* Slot 0 remains reserved on failure. */
    seq[7] = 1;
    CHECK(SsSeqOpen(seq_words, 0) == 1 && SsSeqOpen(seq_words, 0) == -1);
    SsSeqClose(0); SsSeqClose(1);
    seq[8] = 0; seq[9] = 1; seq[15] = 0; /* Slow resolution takes alternate timing branch. */
    CHECK(SsSeqOpen(seq_words, 0) == 0);
    CHECK(get16(table + 0x52) == 1200 && get16(table + 0x54) == 1200);
    SsSeqClose(0);
}

static void voice_attributes(void)
{
    SpuVoiceAttr a = {.voice = (1u << 3) | (1u << 20), .mask = 0x70093,
        .volume = {1234, -2}, .pitch = 0x1234, .addr = 0x2011, .loop_addr = 0x3001,
        .adsr1 = 0xabcd, .adsr2 = 0x1234};
    SpuSetVoiceAttr(&a);
    for (int i = 3; i <= 20; i += 17) {
        CHECK(spu_read16(i * 16) == 1234 && spu_read16(i * 16 + 2) == 0x7ffe);
        CHECK(spu_read16(i * 16 + 4) == 0x1234 && spu_read16(i * 16 + 6) == 0x403);
        CHECK(spu_read16(i * 16 + 8) == 0xabcd && spu_read16(i * 16 + 10) == 0x1234);
        CHECK(spu_read16(i * 16 + 14) == 0x601);
    }
    a.voice = 1u << 3; a.mask = 0xff00; a.ar = a.dr = a.sr = a.rr = a.sl = 255;
    a.a_mode = 5; a.s_mode = 7; a.r_mode = 3;
    SpuSetVoiceAttr(&a);
    CHECK(spu_read16(0x38) == 0xffff && spu_read16(0x3a) == 0xdfdf);
    a.mask = 0x400 | 0x4000; a.r_mode = 7;
    SpuSetVoiceAttr(&a);
    CHECK(spu_read16(0x3a) == 0xdfff);
    a.mask = 0x60; a.sample_note = 0x3c00; a.note = 0x4800;
    SpuSetVoiceAttr(&a); CHECK(spu_read16(0x34) == 0x2000);
    a.note = 0x3000; SpuSetVoiceAttr(&a); CHECK(spu_read16(0x34) == 0x800);
    a.mask = 0x20; a.note = 0x5400; SpuSetVoiceAttr(&a); CHECK(spu_read16(0x34) == 0x3fff);
    a.note = 0x7f00; SpuSetVoiceAttr(&a); CHECK(spu_read16(0x34) == 0); /* Retail truncates base to 16 bits. */
    CHECK(SpuSetNoiseClock(-1) == 0 && SpuSetNoiseClock(99) == 63);
    CHECK((spu_read16(0x1aa) & 0x3f00) == 0x3f00);
    CHECK(SpuSetNoiseVoice(8, 0x100001) == 0x100001);
    CHECK(SpuSetNoiseVoice(1, 8) == 0x100009 && SpuSetNoiseVoice(0, 1) == 0x100008);
    CHECK(SpuSetNoiseVoice(9, 0) == 0x100008);
}

static void explicit_voices(void)
{
    SsInit(); /* Initializes the allocator and all voice registers, preserving RAM. */
    CHECK(spu_read16(0x180) == 0x3fff && spu_read16(0x1aa) == 0xc000);
    CHECK(spu_read16(0x178) == 0x80ff && spu_read16(0x17a) == 0x4000);
    CHECK(SpuMalloc(16) == 0x1010); SpuFree(0x1010);
    header(5, 0x70); vh[0x18] = 127;
    unsigned char *p = vh + 0x20 + 3 * 16, *t = vh + 0x20 + 128 * 16;
    p[1] = 127; p[4] = 64;
    t[0] = 10; t[1] = 4; t[2] = 127; t[3] = 64; t[4] = 60;
    put16(t + 16, 0x80ff); put16(t + 18, 0x4000); put16(t + 22, 2);
    memset(vb, 0, sizeof(vb)); vb[1] = 3; vb[17] = 3;
    CHECK(SsVabOpenHeadSticky(vh, 0, 0x3000) == 0);
    CHECK(SsUtKeyOnV(10, 0, 3, 0, 60, 0, 127, 127) == -1); /* Header-only. */
    CHECK(SsVabTransBody(vb, 0) == 0 && SsVabTransCompleted(1) == 1);
    CHECK(SsUtKeyOnV(-1, 0, 3, 0, 60, 0, 127, 127) == -1);
    CHECK(SsUtKeyOnV(24, 0, 3, 0, 60, 0, 127, 127) == -1);
    CHECK(SsUtKeyOnV(10, 16, 3, 0, 60, 0, 127, 127) == -1);
    CHECK(SsUtKeyOnV(10, 0, -1, 0, 60, 0, 127, 127) == -1);
    CHECK(SsUtKeyOnV(10, 0, 3, 0, 60, 0, 127, 127) == 10);
    CHECK(!(spu_keys() & (1u << 10))); /* Deferred until the tick. */
    SsSeqCalledTbyT();
    CHECK(spu_keys() & (1u << 10));
    CHECK(spu_read16(0xa0) == 16383 && spu_read16(0xa2) == 16383);
    CHECK(spu_read16(0xa4) == 0x1000 && spu_read16(0xa6) == 0x602);
    CHECK(spu_read16(0xa8) == 0x80ff && spu_read16(0xaa) == 0x4000);
    CHECK(spu_read16(0x198) & (1u << 10));
    CHECK(SsUtKeyOnV(23, 0, 3, 0, 72, 0, 127, 0) == 23);
    SsSeqCalledTbyT();
    CHECK(spu_read16(0x174) == 0x2000 && spu_read16(0x172) == 0);
    CHECK(SsUtKeyOnV(11, 0, 3, 0, 60, 8, 127, 127) == 11);
    SsSeqCalledTbyT(); CHECK(spu_read16(0xb4) == 0x100e);
    CHECK(SsUtKeyOffV(-1) == -1 && SsUtKeyOffV(24) == -1);
    CHECK(SsUtKeyOffV(23) == 0 && (spu_keys() & (1u << 23)));
    SsSeqCalledTbyT(); CHECK(!(spu_keys() & (1u << 23)));
    CHECK(SsUtAutoVol(24, 3, 0, 3) == -1 && SsUtAutoVol(10, 3, 0, 0) == -1);
    CHECK(SsUtAutoVol(10, 3, 0, 3) == 0);
    SsSeqCalledTbyT(); CHECK(spu_read16(0xa0) == 245 && spu_read16(0xa2) == 258);
    SsSeqCalledTbyT(); CHECK(spu_read16(0xa0) == 122 && spu_read16(0xa2) == 129);
    SsSeqCalledTbyT(); CHECK(spu_read16(0xa0) == 0 && spu_read16(0xa2) == 0);
    CHECK(SsUtAutoVol(10, 0, 2, 6) == 0);
    for (int i = 0; i < 3; i++) SsSeqCalledTbyT();
    CHECK(spu_read16(0xa2) == 0);
    SsSeqCalledTbyT(); CHECK(spu_read16(0xa2) == 129);
    for (int i = 0; i < 4; i++) SsSeqCalledTbyT();
    CHECK(spu_read16(0xa2) == 258);
    put16(t + 22, 0xff); /* Noise VAG sentinel. */
    CHECK(SsUtKeyOnV(12, 0, 3, 0, 65, 0, 127, 127) == 12);
    SsSeqCalledTbyT(); CHECK(spu_read16(0x194) == (1u << 12));
    CHECK((spu_read16(0x1aa) & 0x3f00) == 0x500);
    SsUtAllKeyOff(9);
    CHECK(spu_read16(0xa0) == 0 && spu_read16(0xa4) == 0x1000);
    SsSeqCalledTbyT(); CHECK(spu_keys() == 0);
    SsVabClose(0);
}

static void tick_clock(void)
{
    SsSetTickMode(2); SsStart();
    FILE *trace = tmpfile(); CHECK(trace != NULL);
    CHECK(spu_capture_begin(trace, NULL) == 0);
    clock_advance(CLOCK_HZ);
    CHECK(spu_capture_end() == 0);
    rewind(trace);
    char line[160]; unsigned ticks = 0;
    while (fgets(line, sizeof(line), trace)) {
        unsigned vb_index, sample, reg, value;
        if (sscanf(line, "%u %u %x %x", &vb_index, &sample, &reg, &value) == 4 && reg == 0x188) ticks++;
    }
    fclose(trace); CHECK(ticks == 240);
    SsEnd();
    CHECK(spu_capture_begin(trace = tmpfile(), NULL) == 0);
    clock_advance(CLOCK_HZ / 60);
    CHECK(spu_capture_end() == 0);
    rewind(trace); CHECK(fgets(line, sizeof(line), trace) != NULL); /* Header only. */
    CHECK(fgets(line, sizeof(line), trace) == NULL); fclose(trace);
    SsSetTickMode(0); SsStart(); SsEnd();
}

static uint32_t seq_words[128];

static unsigned char *sequence_fixture(const unsigned char *events, size_t size, unsigned resolution)
{
    SsInit(); SsSetTickMode(0x1002); /* Manual 240 Hz, same timing as RCnt2. */
    memset(seq_table, 0, sizeof(seq_table));
    SsSetTableSize((char *)seq_table, 2, 2);
    header(5, 0x70); vh[24] = 127;
    unsigned char *p = vh + 0x20 + 3 * 16, *t = vh + 0x20 + 128 * 16;
    p[1] = 127; p[4] = 64;
    t[0] = 10; t[1] = 4; t[2] = 127; t[3] = 64; t[4] = 60; t[7] = 127;
    t[12] = t[13] = 2;
    put16(t + 16, 0x7f0f); put16(t + 18, 0x4000); put16(t + 22, 1);
    memset(vb, 0, sizeof(vb)); vb[1] = 3; vb[17] = 3;
    CHECK(SsVabOpenHeadSticky(vh, 0, 0x3000) == 0);
    CHECK(SsVabTransBody(vb, 0) == 0 && SsVabTransCompleted(1) == 1);
    memset(seq_words, 0, sizeof(seq_words));
    unsigned char *seq = (unsigned char *)seq_words;
    seq[0] = 'p'; seq[7] = 1; seq[8] = resolution >> 8; seq[9] = resolution;
    seq[10] = 7; seq[11] = 0xa1; seq[12] = 0x20; /* 120 BPM. */
    seq[13] = 4; seq[14] = 2; seq[15] = 0;
    CHECK(size <= sizeof(seq_words) - 16);
    memcpy(seq + 16, events, size);
    CHECK(SsSeqOpen((unsigned long *)seq_words, 0) == 0);
    SsSeqPlay(0, 1, 1);
    return (unsigned char *)seq_table;
}

static unsigned key_register(unsigned offset)
{
    return spu_read16(offset) | ((unsigned)spu_read16(offset + 2) << 16);
}

/* 240 PPQN, 120 BPM and 240 callbacks/s give 20 internal units/callback.
 * A delta of four is 40 units. At the equality boundary DW1 dispatches now,
 * retains the old remainder, and flushes the new writes one callback later.
 * The tempo event halves the interval to 10. NRPN 99=20/30 marks a loop;
 * NRPN 98=2 supplies its repeat count. These are DW1 SEQ bytes, not SMF. */
static void sequence_events(void)
{
    static const unsigned char events[] = {
        0xc0, 3, 0,
        0x90, 60, 127, 4,
        60, 0, 4,                       /* Running note-off. */
        0xff, 0x51, 15, 0x42, 0x40, 0, /* 1000000 us/quarter => 60 BPM. */
        0xb0, 99, 20, 0, 98, 2, 0,
        0x90, 72, 127, 4, 72, 0, 4,
        0xb0, 99, 30, 0,
        0xff, 0x2f, 0
    };
    unsigned char *q = sequence_fixture(events, sizeof(events), 240);
    unsigned last_voice = 0;
    for (unsigned tick_index = 1; tick_index <= 24; tick_index++) {
        SsSeqCalledTbyT();
        unsigned on = key_register(0x188), off = key_register(0x18c);
        if (tick_index == 2 || tick_index == 5 || tick_index == 14) {
            CHECK(on == 1); /* Silent ENVX permits reusing the first voice. */
            last_voice = on;
            CHECK(spu_read16(4) == (tick_index == 2 ? 0x1000 : 0x2000));
            CHECK(spu_read16(0) == 16383 && spu_read16(2) == 16383);
            CHECK(spu_read16(6) == 0x600 && spu_read16(8) == 0x7f0f && spu_read16(10) == 0x4000);
            CHECK(spu_read16(0x198) == 1);
        } else CHECK(on == 0);
        if (tick_index == 3 || tick_index == 10 || tick_index == 18 || tick_index == 22) CHECK(off == last_voice);
        else CHECK(off == 0);
        if (tick_index == 1) CHECK(q[0x37] == 3 && get32(q + 0x90) == 20);
        if (tick_index == 4) {
            CHECK(get32(q + 0x94) == 60 && get32(q + 0x8c) == 120);
            CHECK(get16(q + 0x54) == 10 && get16(q + 0x56) == 20);
            CHECK(q[0x1d] == 2 && get32(q + 0x90) == 50);
        }
        if (tick_index == 13) CHECK(q[0x1d] == 1);
        if (tick_index >= 21) {
            CHECK(get32(q + 0x98) == 0 && q[0x14] == 0);
            CHECK(get16(q + 0x54) == 20 && get32(q + 0x94) == 120);
            CHECK(get32(q) == get32(q + 4)); /* Stop rewinds to start. */
        }
    }
    CHECK(spu_keys() == 0);
    SsSeqClose(0); SsVabClose(0);
}

static void sequence_repeats(void)
{
    static const unsigned char events[] = {0xc0, 3, 0, 0x90, 60, 127, 4, 60, 0, 4, 0xff, 0x2f, 0};
    for (int repeat = 0; repeat <= 2; repeat++) {
        unsigned char *q = sequence_fixture(events, sizeof(events), 240);
        SsSeqPlay(0, 1, repeat);
        for (int i = 1; i <= 12; i++) {
            SsSeqCalledTbyT();
            if (!repeat) CHECK(get32(q + 0x98) == 1 && q[0x14] == 1);
            else if (i >= repeat * 4) CHECK(get32(q + 0x98) == 0 && q[0x14] == 0);
            else CHECK(get32(q + 0x98) == 1);
        }
        SsSeqStop(0); SsSeqCalledTbyT(); SsSeqCalledTbyT();
        CHECK(spu_keys() == 0);
        SsSeqClose(0); SsVabClose(0);
    }
    /* RCnt2 drives the same dispatch without manually calling the callback. */
    sequence_fixture(events, sizeof(events), 240);
    SsSetTickMode(2); SsStart();
    clock_advance(CLOCK_HZ / 240); CHECK(key_register(0x188) == 0);
    clock_advance(CLOCK_HZ / 240); CHECK(key_register(0x188) == 1);
    SsEnd(); SsSeqStop(0); SsSeqCalledTbyT(); SsSeqCalledTbyT();
    SsSeqClose(0); SsVabClose(0);
}

static void sequence_controllers(void)
{
    static const unsigned char events[] = {
        0xc0, 3, 0, 0x90, 60, 127, 4,
        0xb0, 7, 64, 2, 10, 0, 2, 11, 32, 2,
        0xe0, 42, 127, 2, /* LSB ignored, bend up two semitones. */
        0xb0, 100, 0, 0, 101, 0, 0, 6, 12, 0,
        0xe0, 0, 127, 2,  /* RPN 0 changed the range to an octave. */
        0xb0, 64, 127, 0, 0x90, 72, 100, 2,
        0xb0, 91, 64, 2, 121, 0, 2,
        0, 1, 0, 0x90, 40, 127, 2, /* Unopened VAB: no key-on. */
        0xb0, 0, 0, 0, 0xc0, 3, 0,
        0x90, 60, 0, 0, 72, 0, 2, 0xff, 0x2f, 0
    };
    unsigned char *q = sequence_fixture(events, sizeof(events), 240);
    unsigned char *p = vh + 0x20 + 3 * 16, *t = vh + 0x20 + 128 * 16;
    for (int i = 1; i <= 14; i++) {
        SsSeqCalledTbyT();
        if (i == 2) CHECK(spu_read16(0) == 16383 && get16(q + 0x60) == 64);
        if (i == 3) CHECK(spu_read16(0) == 4160 && spu_read16(2) == 4160 && q[0x27] == 0);
        if (i == 4) CHECK(spu_read16(0) == 4160 && spu_read16(2) == 1 && p[1] == 32);
        if (i == 5) CHECK(spu_read16(0) == 264 && spu_read16(2) == 0);
        if (i == 6) CHECK(spu_read16(4) == 0x11f5 && t[12] == 12 && t[13] == 12);
        if (i == 7) CHECK(spu_read16(4) == 0x2000);
        if (i == 8) CHECK(key_register(0x188) == 2 && spu_read16(0x1a) == 0x4002);
        if (i == 9) CHECK(spu_read16(0x184) == 16512 && spu_read16(0x186) == 16512);
        if (i == 10) {
            CHECK(!(spu_read16(0x1aa) & 0x80));
            CHECK(q[0x37] == 0 && q[0x27] == 64 && get16(q + 0x60) == 127);
        }
        if (i == 11) CHECK(key_register(0x188) == 0 && q[0x26] == 0);
    }
    CHECK(get32(q + 0x98) == 0 && spu_keys() == 0);
    SsSeqClose(0); SsVabClose(0);

    /* Every NRPN attribute is selected by 99=tone, 98=attribute, 6=value.
     * Two MIDI ticks between data entries isolate each VH/SPU mutation. */
    static const unsigned char values[20] = {7, 4, 10, 100, 20, 30, 3, 4, 5, 6, 7, 8, 70, 9, 10, 7, 64, 32, 16, 32};
    unsigned char nrpn[190] = {0xc0, 3, 0, 0xb0};
    size_t n = 4;
    for (unsigned attr = 0; attr < 20; attr++) {
        nrpn[n++] = 99; nrpn[n++] = 0; nrpn[n++] = 0;
        nrpn[n++] = 98; nrpn[n++] = attr; nrpn[n++] = 0;
        nrpn[n++] = 6; nrpn[n++] = values[attr]; nrpn[n++] = 2;
    }
    nrpn[n++] = 0xff; nrpn[n++] = 0x2f; nrpn[n++] = 0;
    sequence_fixture(nrpn, n, 240);
    for (int i = 0; i < 20; i++) {
        SsSeqCalledTbyT();
        switch (i) {
        case 0: CHECK(t[0] == 7); break;
        case 1: CHECK(t[1] == 4 && (spu_read16(0x1aa) & 0x80)); break;
        case 2: CHECK(t[6] == 10); break;
        case 3: CHECK(t[7] == 100); break;
        case 4: CHECK(get16(t + 16) == 0x140f); break;
        case 5: CHECK(get16(t + 16) == 0x9e0f); break;
        case 6: CHECK(get16(t + 16) == 0x9e3f); break;
        case 7: CHECK(get16(t + 16) == 0x9e34); break;
        case 8: CHECK(get16(t + 18) == 0x4140); break;
        case 9: CHECK(get16(t + 18) == 0xc180); break;
        case 10: CHECK(get16(t + 18) == 0xc187); break;
        case 11: case 12: CHECK(get16(t + 18) == 0xc1a8); break;
        case 13: CHECK(t[9] == 9 && get16(t + 16) == 0x9e34); break;
        case 14: CHECK(t[10] == 10 && get16(t + 18) == 0xc1a8); break;
        case 15: CHECK(spu_read16(0x1a2) == 0xcff8); break;
        case 16: CHECK(spu_read16(0x184) == 16512); break;
        case 17: CHECK(spu_read16(0x1ce) == 8321); break;
        case 18: CHECK(spu_read16(0x1d6) == 515); break;
        case 19: CHECK(spu_read16(0x1d6) == 1031); break;
        }
    }
    SsSeqCalledTbyT(); SsSeqClose(0); SsVabClose(0);
}

int main(void)
{
    clock_reset(); psyq_libspu_reset(); SpuStart();
    for (unsigned i = 0; i < sizeof(vb); i++) vb[i] = i ^ 0x5a;
    allocator(); transfer(); banks(); master_volume();
    voice_attributes(); explicit_voices(); sequence_records(); tick_clock();
    sequence_events(); sequence_repeats(); sequence_controllers();
    puts("libsnd L1 VAB/SEQ events/voices/timer: OK");
    return 0;
}
