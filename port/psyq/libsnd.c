#include "psyq.h"
#include "clock.h"
#include <stdint.h>

/* asm: asm/us/main/psyq/libsnd.s: _svm_vab_* state, represented
 * with native pointers rather than retail RAM addresses. */
typedef struct {
    unsigned char *header, *programs, *tones;
    uint32_t start, total;
    uint8_t used;
} SndBank;
static SndBank banks[16];
static short max_programs;

/* asm: asm/us/main/psyq/libsnd.s: little-endian VH halfword/word accesses. */
static uint16_t vh16(const unsigned char *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t vh32(const unsigned char *p) { return vh16(p) | ((uint32_t)vh16(p + 2) << 16); }
static void vh_put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void vh_put32(unsigned char *p, uint32_t v) { vh_put16(p, v); vh_put16(p + 2, v >> 16); }

/* stub-m2: silent shutdown; no sequencer state. */
void SsEnd(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent initialization; no sequencer state. */
void SsInit(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent shutdown; no resources. */
void SsQuit(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent start; no sequencer. */
void SsStart(void)
{
    clock_psyq_entry();
}

/* stub-m2: close silently; no resources. */
void SsSeqClose(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: stop silently. */
void SsSeqStop(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* asm: asm/us/main/psyq/libsnd.s: SsVabClose. Header-only banks (used=2)
 * are deliberately not closed by this entry, as in DW1. */
void SsVabClose(short id)
{
    clock_psyq_entry();
    if ((unsigned short)id >= 16 || banks[id].used != 1) return;
    SpuFree(banks[id].start);
    banks[id].used = 0;
}

/* stub-m2: silence all voices; no state. */
void SsUtAllKeyOff(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: accept playback without sound. */
void SsSeqPlay(short id, char mode, short repeat)
{
    clock_psyq_entry();
    (void)id;
    (void)mode;
    (void)repeat;
}

/* stub-m2: ignore sequence volume. */
void SsSeqSetVol(short id, short left, short right)
{
    clock_psyq_entry();
    (void)id;
    (void)left;
    (void)right;
}

/* asm: asm/us/main/psyq/libsnd.s: SsSetMVol, signed inputs multiplied by 129. */
void SsSetMVol(short left, short right)
{
    clock_psyq_entry();
    SpuCommonAttr attr = {.mask = 3, .mvol = {(short)(left * 129), (short)(right * 129)}};
    SpuSetCommonAttr(&attr);
}

/* stub-m2: ignore sequencer table. */
void SsSetTableSize(char *table, short seqs, short seps)
{
    clock_psyq_entry();
    (void)table;
    (void)seqs;
    (void)seps;
}

/* stub-m2: ignore sequencer tick mode. */
void SsSetTickMode(long mode)
{
    clock_psyq_entry();
    (void)mode;
}

/* stub-m2: return silent sequence id 0. */
short SsSeqOpen(unsigned long *data, short vabid)
{
    clock_psyq_entry();
    (void)data;
    (void)vabid;
    return 0;
}

/* asm: asm/us/main/psyq/libsnd.s: SsVabOpenHeadWithMode, sticky branch.
 * The header is writable: program indices and sample endpoints are patched in place.
 * This entry does not allocate the caller-specified SPU address. */
static short vab_open_sticky(unsigned char *data, short id, uint32_t addr)
{
    if (psyq_spu_get_in_transfer()) return -1;
    psyq_spu_set_in_transfer(1);
    if (id == -1) {
        for (id = 0; id < 16 && banks[id].used; id++);
    }
    /* Retail indexes before the array for ids < -1; reject instead of C UB. */
    if ((unsigned short)id >= 16 || banks[id].used) {
        psyq_spu_set_in_transfer(0);
        return -1;
    }
    SndBank *b = &banks[id];
    b->used = 1;
    b->header = data;
    if (!data || (vh32(data) >> 8) != 0x564142) goto fail;
    max_programs = data[0] == 0x70 && (int32_t)vh32(data + 4) >= 5 ? 128 : 64;
    unsigned programs = vh16(data + 0x12);
    if (programs > (unsigned)max_programs) goto fail;
    b->programs = data + 0x20;
    b->tones = b->programs + max_programs * 16;
    unsigned index = 0;
    for (int i = 0; i < max_programs; i++) {
        unsigned char *p = b->programs + i * 16;
        vh_put32(p + 8, index);
        if (p[0]) index++;
    }
    unsigned char *sizes = b->tones + programs * 512;
    unsigned samples = data[0x16]; /* lbu in DW1, not the VH's full halfword. */
    uint32_t total = 0;
    unsigned shift = (int32_t)vh32(data + 4) < 5 ? 2 : 3;
    for (unsigned i = 0; i <= samples; i++) total += (uint32_t)vh16(sizes + i * 2) << shift;
    if ((uint32_t)(addr + total) > 0x80000) goto fail;
    b->start = addr;
    total = 0;
    for (unsigned i = 0; i <= samples; i++) {
        total += (uint32_t)vh16(sizes + i * 2) << shift;
        vh_put16(b->programs + (i / 2) * 16 + 12 + (i & 1) * 2, (addr + total) >> 3);
    }
    b->total = total;
    b->used = 2;
    return id;
fail:
    b->used = 0;
    psyq_spu_set_in_transfer(0);
    return -1;
}

/* asm: asm/us/main/psyq/libsnd.s: SsVabOpenHeadSticky. */
short SsVabOpenHeadSticky(unsigned char *data, short vabid, unsigned long addr)
{
    clock_psyq_entry();
    return vab_open_sticky(data, vabid, addr);
}

/* asm: asm/us/main/psyq/libsnd.s: SsVabTransBody. */
short SsVabTransBody(unsigned char *data, short vabid)
{
    clock_psyq_entry();
    /* Retail accepts id=16 and reads beyond the 16 banks: reject safely. */
    if ((unsigned short)vabid >= 16 || banks[vabid].used != 2 || !data) {
        psyq_spu_set_in_transfer(0);
        return -1;
    }
    SpuSetTransferMode(0);
    SpuSetTransferStartAddr(banks[vabid].start);
    SpuWrite(data, banks[vabid].total);
    banks[vabid].used = 1;
    return vabid;
}

/* asm: asm/us/main/psyq/libsnd.s: SsVabTransCompleted. */
short SsVabTransCompleted(short mode)
{
    clock_psyq_entry();
    return (short)SpuIsTransferCompleted(mode);
}

/* asm: asm/us/main/psyq/libsnd.s: SsUtGetVBaddrInSB. */
unsigned long SsUtGetVBaddrInSB(short vabid)
{
    clock_psyq_entry();
    if ((unsigned short)vabid >= 16 || banks[vabid].used != 1) return 0xfffffffful;
    return banks[vabid].start;
}

/* stub-m2: return 0 without sound. */
short SsUtKeyOffV(short voice)
{
    clock_psyq_entry();
    (void)voice;
    return 0;
}

/* stub-m2: return 0 without changing volume. */
short SsUtAutoVol(short voice, short start, short end, short time)
{
    clock_psyq_entry();
    (void)voice;
    (void)start;
    (void)end;
    (void)time;
    return 0;
}

/* stub-m2: return 0 without sound. */
short SsUtKeyOnV(short voice, short vabid, short prog, short tone, short note, short fine, short left, short right)
{
    clock_psyq_entry();
    (void)voice;
    (void)vabid;
    (void)prog;
    (void)tone;
    (void)note;
    (void)fine;
    (void)left;
    (void)right;
    return 0;
}

