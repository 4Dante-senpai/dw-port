#include "psyq.h"
#include "clock.h"
#include <stdint.h>
#include <string.h>

/* asm: asm/us/main/psyq/libsnd.s: _ss_score, DW1's 0xb0-byte SEP record.
 * Pointers remain native; named ranges preserve the retail offsets. */
typedef struct {
    unsigned char *cursor, *start, *loop, *next, *saved;
    unsigned char state[0x34];
    short fade[4], resolution, repeat, interval, step;
    short volume[4], channel_volume[16], play_count, reserved;
    int32_t delta, elapsed, tempo, initial_delta, initial_tempo, flags;
    int32_t crescendo, decrescendo, tempo_change, next_seq, next_sep;
} SndSeq;
_Static_assert(sizeof(SndSeq) == 0xb0 && offsetof(SndSeq, flags) == 0x98,
               "DW1 sequence layout");
static SndSeq *scores[32];
static short seq_count, sep_count;
static uint32_t open_mask;
static struct { long mode, manual, rate; unsigned char irq, vsync, half; }
    tick = {60, 1, 60, 0x7f, 0, 0};
static unsigned tick_half_phase;

/* asm: asm/us/main/psyq/libsnd.s: _svm_vab_* state, represented
 * with native pointers rather than retail RAM addresses. */
typedef struct {
    unsigned char *header, *programs, *tones;
    uint32_t start, total;
    uint8_t used;
} SndBank;
static SndBank banks[16];
static short max_programs;
static SpuReverbAttr snd_reverb;

/* asm: asm/us/main/psyq/libsnd.s: little-endian VH halfword/word accesses. */
static uint16_t vh16(const unsigned char *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t vh32(const unsigned char *p) { return vh16(p) | ((uint32_t)vh16(p + 2) << 16); }
static void vh_put16(unsigned char *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void vh_put32(unsigned char *p, uint32_t v) { vh_put16(p, v); vh_put16(p + 2, v >> 16); }

#include "libsnd_vm.inc"
#include "libsnd_seq.inc"
#include "libsnd_events.inc"

/* asm: asm/us/main/psyq/libsnd.s: SsEnd. SsStart(0) uses the VSync slot
 * for irq=0, so the alternate SsStart2 interrupt-chain branch is unreachable. */
void SsEnd(void)
{
    clock_psyq_entry();
    if (tick.manual || tick.irq == 0x7f) return;
    tick.half = 0;
    if (tick.vsync) { VSyncCallback(NULL); tick.vsync = 0; }
    else InterruptCallback(6, NULL);
    tick.irq = 0x7f;
}

/* asm: asm/us/main/psyq/libsnd.s: SsInit -> _SsInit, _SsVmInit(24). */
void SsInit(void)
{
    clock_psyq_entry();
    ResetCallback();
    SpuInit();
    static const uint16_t initial[8] = {0, 0, 0x1000, 0x3000, 0xbf, 0, 0, 0};
    for (unsigned v = 0; v < 24; v++) for (unsigned r = 0; r < 8; r++)
        spu_write16(v * 16 + r * 2, initial[r]);
    for (unsigned r = 0; r < 16; r++) spu_write16(0x180 + r * 2, r < 2 ? 0x3fff : 0);
    vm_init();
    open_mask = 0;
    tick.rate = 60;
}

/* asm: asm/us/main/psyq/libsnd.s: SsQuit. */
void SsQuit(void)
{
    clock_psyq_entry();
    SpuQuit();
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqCalledTbyT. */
void SsSeqCalledTbyT(void)
{
    if (vm_busy) return;
    vm_busy = 1;
    vm_flush();
    for (int s = 0; s < seq_count; s++) {
        if (!(open_mask & (1u << s))) continue;
        for (int t = 0; t < sep_count; t++) {
            SndSeq *q = scores[s] + t;
            short id = s | (t << 8);
            if (q->flags & 1) {
                snd_play(id, q);
                if (q->flags & 0x10) snd_fade(id, q, 0);
                if (q->flags & 0x20) snd_fade(id, q, 1);
                if (q->flags & 0x40) snd_tempo_fade(q);
                if (q->flags & 0x80) snd_tempo_fade(q);
            }
            if (q->flags & 2) { vm_seq_key_off(id); q->state[0] = 0; q->flags &= ~2; }
            if (q->flags & 8) { q->state[0] = 1; q->flags &= ~8; }
            if (q->flags & 4) { snd_stop(s, t); q->flags = 0; }
        }
    }
    vm_busy = 0;
}

/* asm: asm/us/main/psyq/libsnd.s: _SsSeqCalledTbyT_1per2. */
static void snd_half_tick(void)
{
    if (!tick_half_phase) tick_half_phase = 1;
    else { tick_half_phase = 0; SsSeqCalledTbyT(); }
}

/* asm: asm/us/main/psyq/libsnd.s: SsStart -> _SsStart(0). */
void SsStart(void)
{
    clock_psyq_entry();
    tick.irq = 6; tick.vsync = tick.half = 0;
    if (!tick.mode) { tick.irq = 0x7f; return; }
    if (tick.mode == 5) {
        tick.irq = 0; tick.vsync = 1;
        VSyncCallback(SsSeqCalledTbyT);
        return;
    }
    unsigned target = 0x44e8;
    if (tick.mode == 3) target = 0x89d0;
    else if (tick.mode != 2) {
        if (tick.manual) return;
        if (tick.mode < 70) { target = 0x204cc0 / tick.mode; tick.half = 1; }
        else target = 0x409980 / tick.mode;
    }
    ResetRCnt(RCntCNT2);
    SetRCnt(RCntCNT2, target, RCntMdINTR);
    InterruptCallback(6, tick.half ? snd_half_tick : SsSeqCalledTbyT);
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqClose -> _SsClose. */
void SsSeqClose(short id)
{
    clock_psyq_entry();
    if ((unsigned short)id >= seq_count || !scores[id]) return;
    vm_seq_volume(id, 0, 0);
    vm_seq_key_off(id);
    open_mask &= ~(1u << id);
    for (int i = 0; i < sep_count; i++) {
        SndSeq *q = scores[id] + i;
        q->flags = 0; q->state[0x22 - 0x14] = 0xff; q->state[0x23 - 0x14] = 0;
        memset(q->fade, 0, sizeof(q->fade));
        q->crescendo = q->decrescendo = q->tempo_change = q->next_seq = q->next_sep = 0;
        q->volume[0] = q->volume[1] = 127;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqStop -> _SsSndStop(id, 0). */
void SsSeqStop(short id)
{
    clock_psyq_entry();
    if ((unsigned short)id < seq_count && scores[id]) snd_stop(id, 0);
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

/* asm: asm/us/main/psyq/libsnd.s: SsUtAllKeyOff. The argument is unused. */
void SsUtAllKeyOff(short id)
{
    clock_psyq_entry();
    (void)id;
    for (int i = 0; i < vm_max; i++) {
        if (vm_reserved & (1u << i)) continue;
        SndVoice *v = voices + i;
        v->age = 24; v->env = 0; v->seq = 0xff;
        v->compact = v->program = 0; v->tone = 0xff;
        vm_clear_voice(i);
        vm_key_off(i);
    }
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqPlay -> Snd_SetPlayMode. */
void SsSeqPlay(short id, char mode, short repeat)
{
    clock_psyq_entry();
    if ((unsigned short)id >= seq_count || !scores[id]) return;
    SndSeq *q = scores[id];
    q->cursor = q->loop = q->next = q->start;
    q->flags &= ~(0x200 | 4);
    q->state[0x20 - 0x14] = repeat;
    if (mode == 1) {
        q->flags |= 1;
        q->state[0] = 1; q->state[0x21 - 0x14] = 0;
        vm_seq_volume(id, q->volume[0], q->volume[1]);
    } else if (!mode) q->flags |= 2;
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqSetVol. The exact flags==1
 * comparison is intentional: other flag combinations only update the table. */
void SsSeqSetVol(short id, short left, short right)
{
    clock_psyq_entry();
    if ((unsigned short)id >= seq_count || !scores[id]) return;
    SndSeq *q = scores[id];
    if (q->flags == 1) vm_seq_volume(id, left, right);
    else { q->volume[0] = left; q->volume[1] = right; }
}

/* asm: asm/us/main/psyq/libsnd.s: SsSetMVol, signed inputs multiplied by 129. */
void SsSetMVol(short left, short right)
{
    clock_psyq_entry();
    SpuCommonAttr attr = {.mask = 3, .mvol = {(short)(left * 129), (short)(right * 129)}};
    SpuSetCommonAttr(&attr);
}

/* asm: asm/us/main/psyq/libsnd.s: SsSetTableSize. */
void SsSetTableSize(char *table, short seqs, short seps)
{
    clock_psyq_entry();
    /* Retail writes outside _ss_score for seqs > 32. Reject invalid storage. */
    if (!table || seqs < 0 || seqs > 32 || seps < 0) return;
    seq_count = seqs;
    sep_count = seps;
    for (int s = 0; s < seqs; s++) scores[s] = (SndSeq *)table + s * seps;
    for (int s = seqs; s < 32; s++) open_mask |= 1u << s;
    for (int s = 0; s < seqs; s++) for (int t = 0; t < seps; t++) {
        SndSeq *q = scores[s] + t;
        q->flags = 0;
        q->state[0x22 - 0x14] = 0xff;
        q->state[0x23 - 0x14] = 0;
        memset(q->fade, 0, sizeof(q->fade));
        q->crescendo = q->decrescendo = q->tempo_change = q->next_seq = q->next_sep = 0;
        for (int i = 0; i < 4; i++) q->volume[i] = 127;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: SsSetTickMode; jtbl_80114BA8. */
void SsSetTickMode(long mode)
{
    clock_psyq_entry();
    long video = GetVideoMode();
    tick.manual = !!(mode & 0x1000);
    tick.mode = tick.manual ? mode & 0xfff : mode;
    switch (tick.mode) {
    case 1: tick.rate = 60; tick.mode = video == 0 ? 5 : 60; break;
    case 2: tick.rate = 240; break;
    case 3: tick.rate = 120; break;
    case 4: tick.rate = 50; tick.mode = video == 1 ? 5 : 50; break;
    case 0: case 5: tick.rate = video == 1 ? 50 : 60; break;
    default: tick.rate = tick.mode < 6 ? 60 : tick.mode; break;
    }
}

/* asm: asm/us/main/psyq/libsnd.s: SsSeqOpen. The retail _SsFCALL table
 * contains fixed function pointers; native dispatch will use direct calls. */
short SsSeqOpen(unsigned long *data, short vabid)
{
    clock_psyq_entry();
    if (!data || !sep_count) return -1;
    int id;
    for (id = 0; id < 31 && (open_mask & (1u << id)); id++);
    /* DW1's search rejects slot 31 even when it is the last free bit. */
    if (id >= 31 || id >= seq_count || !scores[id]) return -1;
    open_mask |= 1u << id;
    /* A failed format/version check deliberately leaves the bit allocated. */
    return snd_init_seq(scores[id], (unsigned char *)data, vabid) < 0 ? -1 : id;
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

/* asm: asm/us/main/psyq/libsnd.s: SsUtKeyOffV -> _SsVmKeyOffNow(0). */
short SsUtKeyOffV(short voice)
{
    clock_psyq_entry();
    if (vm_busy) return -1;
    vm_busy = 1;
    if ((unsigned short)voice >= 24) { vm_busy = 0; return -1; }
    vm_key_off(voice);
    vm_busy = 0;
    return 0;
}

/* asm: asm/us/main/psyq/libsnd.s: SsUtAutoVol -> SeAutoVol/SetAutoVol. */
short SsUtAutoVol(short voice, short start, short end, short time)
{
    clock_psyq_entry();
    if ((unsigned short)voice >= 24) return -1;
    /* Retail traps on division by zero. Reject rather than invoking C UB. */
    if (start != end && !time) return -1;
    vm_set_auto_volume(voice, start, end, time);
    return 0;
}

/* asm: asm/us/main/psyq/libsnd.s: SsUtKeyOnV. */
short SsUtKeyOnV(short voice, short vabid, short prog, short tone, short note, short fine, short left, short right)
{
    clock_psyq_entry();
    if (vm_busy) return -1;
    vm_busy = 1;
    if ((unsigned short)voice >= 24 || vm_setup(vabid, prog) < 0) goto fail;
    if ((right < left && !left) || (right > left && !right)) goto fail;
    current.seq = 0x21; current.note = note; current.fine = fine; current.tone = tone;
    if (left == right) { current.volume = left; current.pan = 64; }
    else if (right < left) { current.volume = left; current.pan = (right * 64) / left; }
    else { current.volume = right; current.pan = 127 - (left * 64) / right; }
    unsigned char *p = current_bank->programs + prog * 16;
    current.program_vol = p[1]; current.program_pan = p[4]; current.tones = p[0];
    /* Retail has no bounds check for tone or compact indices. */
    int index = (signed char)current.compact * 16 + (signed char)current.tone;
    if (index < 0 || index >= vh16(current_bank->header + 0x12) * 16) goto fail;
    unsigned char *t = current_bank->tones + index * 32;
    current.priority = t[0]; current.vag = vh16(t + 22);
    if (!current.vag) goto fail;
    current.tone_vol = t[2]; current.tone_pan = t[3]; current.center = t[4];
    current.shift = t[5]; current.mode = t[1]; current.min_note = t[6]; current.max_note = t[7];
    SndVoice *v = voices + voice;
    v->seq = 0x21; v->vab = vabid; v->program = prog;
    v->compact = (signed char)current.compact; v->vag = current.vag;
    v->note = note; v->status = 1; v->age = 0; v->tone = (signed char)current.tone;
    current.voice = voice;
    vm_allocate();
    vm_key_volume(current.vag == 0xff, current.vag == 0xff ? 0 : vm_note_pitch(note, fine));
    vm_busy = 0;
    return voice;
fail:
    vm_busy = 0;
    return -1;
}

