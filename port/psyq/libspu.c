/* DW1 mw_lib41 translations. asm: asm/us/main/psyq/libspu.s.
 * Derived from DW1's asm; no DW2-PC library code is copied here. */
#include "psyq.h"
#include "clock.h"
#include "../backend/spu.h"
#include <string.h>

#include "spu_reverb.inc"
/* asm: asm/us/main/data/psyq/libspu.data.s, _spu_rev_startaddr. */
static const uint16_t reverb_start[10] = {
    0xfffe, 0xfb28, 0xfc18, 0xf6f8, 0xf204, 0xea44, 0xe128, 0xcff8, 0xcff8, 0xf880
};
static SpuReverbAttr reverb;
static int reverb_on;
static uint16_t reverb_offset;
typedef struct { uint32_t addr, size; } SpuBlock;
static SpuBlock *mem_list;
static int block_count, last_block;
static uint16_t transfer_start;
static long transfer_mode;
/* Retail _spu_inTransfer: 1 = available, 0 = reserved/pending. */
static int transfer_available = 1;
static int transfer_event = -1;
static uint64_t transfer_due;
static int allocated_at_or_after(uint32_t addr);

void psyq_libspu_reset(void)
{
    memset(&reverb, 0, sizeof(reverb));
    reverb_on = 0;
    reverb_offset = 0;
    mem_list = NULL;
    block_count = last_block = 0;
    transfer_start = 0;
    transfer_mode = 0;
    transfer_available = 1;
    if (transfer_event >= 0) clock_cancel(transfer_event);
    transfer_event = -1;
    transfer_due = 0;
    spu_reset();
}

/* asm: asm/us/main/psyq/libspu.s: SpuClearReverbWorkArea; _spu_t (DMA chunks), _SpuIsInAllocateArea_.
 * Preserve the mode-0 special 128-byte clear. */
static long clear_work_area(long mode)
{
    if ((unsigned long)mode >= 10) return -1;
    uint32_t start = mode ? (uint32_t)reverb_start[mode] * 8 : 0x7ff80;
    if (allocated_at_or_after((uint32_t)reverb_start[mode] * 8)) return -1;
    uint32_t remaining = SPU_RAM_SIZE - start;
    const uint32_t zeros[256] = {0};
    while (remaining) {
        uint32_t size = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
        transfer_start = start >> 3;
        spu_write16(0x1a6, transfer_start);
        spu_dma_write(zeros, size / 4);
        /* WaitEvent in the asm: the transfer completes on the virtual clock. */
        clock_advance((uint64_t)size * 4);
        remaining -= size;
        start += size;
    }
    return 0;
}
/* asm: asm/us/main/psyq/libspu.s: SpuClearReverbWorkArea (public entry). */
long SpuClearReverbWorkArea(long mode)
{
    clock_psyq_entry();
    return clear_work_area(mode);
}

/* asm: asm/us/main/psyq/libspu.s: SpuGetKeyStatus, first selected voice; _spu_keystat and ENVX. */
long SpuGetKeyStatus(unsigned long mask)
{
    clock_psyq_entry();
    for (unsigned i = 0; i < 24; i++) {
        if (!(mask & (1ul << i))) continue;
        int on = (spu_keys() & (1u << i)) != 0;
        int env = spu_read16(i * 16 + 12) != 0;
        return on ? (env ? SPU_ON : SPU_ON_ENV_OFF) : (env ? SPU_OFF_ENV_ON : SPU_OFF);
    }
    return -1;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetReverb; _SpuIsInAllocateArea_. */
long SpuSetReverb(long on)
{
    clock_psyq_entry();
    if (on == 0 || on == 1) {
        if (on && allocated_at_or_after((uint32_t)reverb_offset * 8)) on = 0;
        reverb_on = on;
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~0x80) | (on ? 0x80 : 0));
    }
    return reverb_on;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetReverbDepth. Also used without a second PsyQ entry by ModeParam. */
static void set_depth(const SpuReverbAttr *attr)
{
    if (!attr->mask || (attr->mask & 2)) {
        reverb.depth.left = attr->depth.left;
        spu_write16(0x184, attr->depth.left);
    }
    if (!attr->mask || (attr->mask & 4)) {
        reverb.depth.right = attr->depth.right;
        spu_write16(0x186, attr->depth.right);
    }
}
/* asm: asm/us/main/psyq/libspu.s: SpuSetReverbDepth (public entry). */
long SpuSetReverbDepth(SpuReverbAttr *attr)
{
    clock_psyq_entry();
    set_depth(attr);
    return 0;
}

/* asm: asm/us/main/psyq/libspu.s: _spu_setReverbAttr, one mask bit per halfword at 0x1c0. */
static void set_reverb_attr(const uint16_t *params, uint32_t mask)
{
    for (unsigned i = 0; i < 32; i++)
        if (!mask || (mask & (1u << i))) spu_write16(0x1c0 + i * 2, params[i]);
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetReverbModeParam, including selective echo/delay parameters. */
long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
    clock_psyq_entry();
    int change = !attr->mask || (attr->mask & 1);
    long mode = change ? (attr->mode & ~0x100L) : reverb.mode;
    if ((unsigned long)mode >= 10) return -1;
    if (change && allocated_at_or_after((uint32_t)reverb_start[mode] * 8)) return -1;
    uint16_t params[32];
    memcpy(params, reverb_presets[mode], sizeof(params));
    uint32_t mask = 0;
    if (change) {
        reverb.mode = mode;
        reverb_offset = reverb_start[mode];
        reverb.delay = (mode == 7 || mode == 8) ? 127 : 0;
        reverb.feedback = mode == 7 ? 127 : 0;
    }
    int delay = (!attr->mask || (attr->mask & 8)) && (mode == 7 || mode == 8);
    int feedback = (!attr->mask || (attr->mask & 16)) && (mode == 7 || mode == 8);
    if (delay) {
        reverb.delay = attr->delay;
        /* MIPS shifts/adds wrap at 32 bits before signed division by 127. */
        int32_t d = (int32_t)((uint32_t)attr->delay << 12) / 127;
        params[10] = (int32_t)((uint32_t)attr->delay << 13) / 127 - params[0];
        params[11] = d - params[1];
        params[16] = reverb_presets[mode][17] + d;
        params[12] = reverb_presets[mode][13] + d;
        params[26] = reverb_presets[mode][28] + d;
        params[27] = reverb_presets[mode][29] + d;
        mask |= 0x0c011c00;
    }
    if (feedback) {
        reverb.feedback = attr->feedback;
        params[7] = (int32_t)((uint32_t)attr->feedback * 33024u) / 127;
        mask |= 0x80;
    }
    uint16_t control = spu_read16(0x1aa);
    if (change) {
        if (control & 0x80) spu_write16(0x1aa, control & ~0x80);
        reverb.depth.left = reverb.depth.right = 0;
        spu_write16(0x184, 0);
        spu_write16(0x186, 0);
    } else set_depth(attr);
    if (change || delay || feedback) set_reverb_attr(params, change ? 0 : mask);
    if (change && (attr->mode & 0x100)) clear_work_area(mode);
    if (change) {
        spu_write16(0x1a2, reverb_start[mode]);
        if (control & 0x80) spu_write16(0x1aa, spu_read16(0x1aa) | 0x80);
    }
    return 0;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetReverbVoice -> _SpuSetAnyVoice(…, 0xcc, 0xcd).
 * SPU_ON=OR, SPU_OFF=AND NOT, SPU_BIT=replace; other modes query. */
unsigned long SpuSetReverbVoice(long on, unsigned long voice)
{
    clock_psyq_entry();
    uint16_t high = spu_read16(0x19a);
    uint32_t bits = spu_read16(0x198) | ((uint32_t)(high & 255) << 16);
    if (on == 1) bits |= voice & 0xffffff;
    else if (on == 0) bits &= ~(voice & 0xffffff);
    else if (on == 8) bits = voice & 0xffffff;
    else return bits;
    spu_write16(0x198, bits);
    spu_write16(0x19a, (bits >> 16) | (on == 8 ? 0 : high & 0xff00));
    return bits;
}

#define BLOCK_TAIL 0x40000000u
#define BLOCK_FREE 0x80000000u
#define BLOCK_ADDR 0x0fffffffu
#define BLOCK_DEAD 0x2fffffffu

/* asm: asm/us/main/psyq/libspu.s: _spu_gcSPU.
 * Merge adjacent holes, sort by address, compact the dead records, and fold
 * trailing holes into the terminal record. The caller supplies num+1 records. */
static void collect_blocks(void)
{
    if (!mem_list) return;
    for (int i = 0; i <= last_block; ) {
        if (mem_list[i].addr & BLOCK_FREE) {
            int next = i + 1;
            while (next <= last_block && mem_list[next].addr == BLOCK_DEAD) next++;
            if (next <= last_block && (mem_list[next].addr & BLOCK_FREE) &&
                (mem_list[next].addr & BLOCK_ADDR) ==
                (mem_list[i].addr & BLOCK_ADDR) + mem_list[i].size) {
                mem_list[next].addr = BLOCK_DEAD;
                mem_list[i].size += mem_list[next].size;
                continue;
            }
        }
        i++;
    }
    for (int i = 0; i <= last_block; i++)
        if (!mem_list[i].size && !(mem_list[i].addr & BLOCK_TAIL)) mem_list[i].addr = BLOCK_DEAD;
    for (int i = 0; i <= last_block; i++) {
        if (mem_list[i].addr & BLOCK_TAIL) break;
        for (int j = i + 1; j <= last_block; j++) {
            if (mem_list[j].addr & BLOCK_TAIL) break;
            if ((mem_list[j].addr & BLOCK_ADDR) < (mem_list[i].addr & BLOCK_ADDR)) {
                SpuBlock tmp = mem_list[i]; mem_list[i] = mem_list[j]; mem_list[j] = tmp;
            }
        }
    }
    for (int i = 0; i <= last_block; i++) {
        if (mem_list[i].addr & BLOCK_TAIL) break;
        if (mem_list[i].addr == BLOCK_DEAD) {
            mem_list[i] = mem_list[last_block];
            last_block = i;
            break;
        }
    }
    for (int i = last_block - 1; i >= 0; i--) {
        if (!(mem_list[i].addr & BLOCK_FREE)) break;
        mem_list[i].addr = (mem_list[i].addr & BLOCK_ADDR) | BLOCK_TAIL;
        mem_list[i].size += mem_list[last_block].size;
        last_block = i;
    }
}

/* asm: asm/us/main/psyq/libspu.s: SpuInitMalloc. */
long SpuInitMalloc(long num, char *top)
{
    clock_psyq_entry();
    if (num <= 0) return 0;
    mem_list = (SpuBlock *)top;
    block_count = num;
    last_block = 0;
    mem_list[0] = (SpuBlock){BLOCK_TAIL | 0x1010, SPU_RAM_SIZE - 0x1010};
    return num;
}

/* asm: asm/us/main/psyq/libspu.s: SpuMalloc; _spu_rev_reserve_wa is
 * zero in DW1 (there is no SpuReserveReverbWorkArea entry in this binary). */
long SpuMalloc(long size)
{
    clock_psyq_entry();
    if (!mem_list) return -1;
    uint32_t bytes = (uint32_t)size;
    /* Preserve retail's test of the upper bits, including sizes 1..7 -> 0. */
    if (bytes & ~7u) bytes += 7;
    bytes &= ~7u;
    /* Avoid retail's zero-sized-record corruption for requests below eight bytes. */
    if (!bytes) return -1;
    if (!(mem_list[0].addr & BLOCK_TAIL)) collect_blocks();
    int i;
    for (i = 0; i < block_count; i++) {
        if (mem_list[i].addr & BLOCK_TAIL) break;
        if ((mem_list[i].addr & BLOCK_FREE) && mem_list[i].size >= bytes) break;
    }
    if (i == block_count) return -1;
    SpuBlock *b = &mem_list[i];
    if (b->addr & BLOCK_TAIL) {
        if (b->size < bytes) return -1;
        mem_list[i + 1] = (SpuBlock){((b->addr & BLOCK_ADDR) + bytes) | BLOCK_TAIL,
                                   b->size - bytes};
        last_block = i + 1;
    } else if (bytes < b->size && last_block < block_count) {
        mem_list[last_block + 1] = mem_list[last_block];
        mem_list[last_block] = (SpuBlock){(b->addr + bytes) | BLOCK_FREE, b->size - bytes};
        last_block++;
    }
    b->addr &= BLOCK_ADDR;
    b->size = bytes;
    collect_blocks();
    return mem_list[i].addr;
}

/* asm: asm/us/main/psyq/libspu.s: SpuFree. Unknown addresses are ignored. */
void SpuFree(unsigned long addr)
{
    clock_psyq_entry();
    for (int i = 0; mem_list && i < block_count; i++) {
        if (mem_list[i].addr & BLOCK_TAIL) break;
        if (mem_list[i].addr == addr) { mem_list[i].addr = addr | BLOCK_FREE; break; }
    }
    collect_blocks();
}

/* asm: asm/us/main/psyq/libspu.s: _SpuIsInAllocateArea_. */
static int allocated_at_or_after(uint32_t addr)
{
    for (int i = 0; mem_list && i <= last_block; i++) {
        uint32_t a = mem_list[i].addr;
        if (a & BLOCK_FREE) continue;
        if (a & BLOCK_TAIL) break;
        a &= BLOCK_ADDR;
        if (a >= addr || addr < a + mem_list[i].size) return 1;
    }
    return 0;
}

/* asm: asm/us/main/psyq/libspu.s: _spu_setInTransfer / _spu_getInTransfer. */
void psyq_spu_set_in_transfer(int busy) { transfer_available = busy == 1 ? 0 : 1; }
int psyq_spu_get_in_transfer(void) { return transfer_available != 1; }

/* asm: asm/us/main/psyq/libspu.s: SpuSetTransferStartAddr / _spu_FsetRXXa.
 * The setter stores TSA only; _spu_Fw writes the hardware register later. */
unsigned long SpuSetTransferStartAddr(unsigned long addr)
{
    clock_psyq_entry();
    if ((uint32_t)(addr - 0x1010) > 0x7efe8) return 0;
    transfer_start = (addr + 7) >> 3;
    return (uint32_t)transfer_start << 3;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetTransferMode. */
long SpuSetTransferMode(long mode)
{
    clock_psyq_entry();
    transfer_mode = mode;
    return mode == 1 ? 1 : 0;
}

/* asm: asm/us/main/psyq/libspu.s: SpuWrite / _spu_Fw / _spu_t.
 * DMA BCR rounds up to 64 bytes, as in DW1. Copy via an aligned local word
 * block so the native source may have the same halfword alignment as PS1. */
/* asm: asm/us/main/psyq/libspu.s: _spu_FiDMA / _SpuDataCallback.
 * DMA completion clears the transfer control bits before signaling completion. */
static void transfer_irq(void *arg)
{
    (void)arg;
    transfer_event = -1;
    spu_write16(0x1aa, spu_read16(0x1aa) & ~0x30);
}

unsigned long SpuWrite(unsigned char *addr, unsigned long size)
{
    clock_psyq_entry();
    if (size > 0x7eff0) size = 0x7eff0;
    spu_write16(0x1a6, transfer_start);
    if (transfer_mode == 1) {
        /* asm: _spu_FwriteByIO, halfword FIFO chunks of at most 64 bytes. */
        uint16_t control = spu_read16(0x1aa) & ~0x30;
        for (unsigned long i = 0; i < size; i += 2) {
            spu_write16(0x1a8, addr[i] | ((uint16_t)addr[i + 1] << 8));
            if ((i & 63) == 62 || i + 2 >= size) spu_write16(0x1aa, control | 0x10);
        }
        spu_write16(0x1aa, control);
    } else {
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~0x30) | 0x20);
        unsigned long rounded = (size + 63) & ~63ul;
        for (unsigned long i = 0; i < rounded; i += 64) {
            uint32_t words[16];
            memcpy(words, addr + i, sizeof(words));
            /* The PS1 transfer cursor wraps at the end of sound RAM. */
            uint32_t pos = (((uint32_t)transfer_start << 3) + i) & (SPU_RAM_SIZE - 1);
            uint32_t first = SPU_RAM_SIZE - pos;
            if (first >= 64) spu_dma_write(words, 16);
            else { spu_dma_write(words, first / 4); spu_dma_write(words + first / 4, (64 - first) / 4); }
        }
        clock_cancel(transfer_event);
        if (transfer_due < clock_now()) transfer_due = clock_now();
        transfer_due += (uint64_t)rounded * 4;
        transfer_event = clock_schedule(transfer_due - clock_now(), transfer_irq, NULL);
    }
    transfer_available = 0;
    return size;
}

/* asm: asm/us/main/psyq/libspu.s: SpuIsTransferCompleted. */
static int transfer_done(void *arg) { (void)arg; return spu_transfer_completed(); }
long SpuIsTransferCompleted(long flag)
{
    clock_psyq_entry();
    if (transfer_mode == 1 || transfer_available == 1) return 1;
    if (flag == 1) clock_wait_until(transfer_done, NULL);
    int done = spu_transfer_completed();
    if (done) transfer_available = 1;
    return done;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetCommonAttr, jump tables
 * jtbl_80114AB8 / jtbl_80114AD8. A sweep clips the rate to 0..127;
 * a direct volume retains bits 0..14 (including the direct phase sign). */
static uint16_t common_volume(short value, short mode)
{
    if (mode < 1 || mode > 7) return (uint16_t)value & 0x7fff;
    unsigned rate = value < 0 ? 0 : value > 127 ? 127 : value;
    return (0x8000 + ((unsigned)(mode - 1) << 12)) | rate;
}

/* asm: asm/us/main/psyq/libspu.s: SpuSetCommonAttr. mvolx is read-only. */
void SpuSetCommonAttr(SpuCommonAttr *attr)
{
    clock_psyq_entry();
    uint32_t mask = attr->mask;
    if (!mask || (mask & 1))
        spu_write16(0x180, common_volume(attr->mvol.left,
            !mask || (mask & 4) ? attr->mvolmode.left : 0));
    if (!mask || (mask & 2))
        spu_write16(0x182, common_volume(attr->mvol.right,
            !mask || (mask & 8) ? attr->mvolmode.right : 0));
    if (!mask || (mask & 0x40)) spu_write16(0x1b0, attr->cd.volume.left);
    if (!mask || (mask & 0x80)) spu_write16(0x1b2, attr->cd.volume.right);
    if (!mask || (mask & 0x400)) spu_write16(0x1b4, attr->ext.volume.left);
    if (!mask || (mask & 0x800)) spu_write16(0x1b6, attr->ext.volume.right);
    if (!mask || (mask & 0x100))
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~4) | (attr->cd.reverb ? 4 : 0));
    if (!mask || (mask & 0x200))
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~1) | (attr->cd.mix ? 1 : 0));
    if (!mask || (mask & 0x1000))
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~8) | (attr->ext.reverb ? 8 : 0));
    if (!mask || (mask & 0x2000))
        spu_write16(0x1aa, (spu_read16(0x1aa) & ~2) | (attr->ext.mix ? 2 : 0));
}
