/* DW1 PsyQ translations. asm: asm/us/main/psyq/libspu.s.
 * The allocator, deferred register queue and libsnd callers arrive in story 3.5;
 * their reset state is an empty allocation list and immediate register access. */
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

void psyq_libspu_reset(void)
{
    memset(&reverb, 0, sizeof(reverb));
    reverb_on = 0;
    spu_reset();
}

/* asm: asm/us/main/psyq/libspu.s: SpuClearReverbWorkArea; _spu_t (DMA chunks), _SpuIsInAllocateArea_.
 * No allocation list exists until 3.5. Preserve the mode-0 special 128-byte clear. */
static long clear_work_area(long mode)
{
    if ((unsigned long)mode >= 10) return -1;
    uint32_t start = mode ? (uint32_t)reverb_start[mode] * 8 : 0x7ff80;
    uint32_t remaining = SPU_RAM_SIZE - start;
    const uint32_t zeros[256] = {0};
    while (remaining) {
        uint32_t size = remaining > sizeof(zeros) ? sizeof(zeros) : remaining;
        spu_write16(0x1a6, start >> 3);
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

/* asm: asm/us/main/psyq/libspu.s: SpuSetReverb; _SpuIsInAllocateArea_ with empty allocation list. */
long SpuSetReverb(long on)
{
    clock_psyq_entry();
    if (on == 0 || on == 1) {
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
    uint16_t params[32];
    memcpy(params, reverb_presets[mode], sizeof(params));
    uint32_t mask = 0;
    if (change) {
        reverb.mode = mode;
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
