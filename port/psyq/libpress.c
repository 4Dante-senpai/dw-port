/* USA PsyQ libpress. Tables are loaded from the user's Pack (AD-12). */
#include "psyq.h"
#include "clock.h"
#include "clock_costs.h"
#include "libpress.h"
#include "port.h"
#include "../backend/mdec.h"
#include "../backend/cdrom.h"
#include "dw1_press_tables.h"
#include <string.h>

static uint8_t mdec_iq[DW1_PRESS_MDEC_IQ_SIZE];
static uint8_t mdec_coef[DW1_PRESS_MDEC_COEF_SIZE];
static uint8_t data_2[DW1_PRESS_DATA_2_SIZE];
static const struct Table {
    const char *name;
    uint8_t *data;
    uint32_t offset, size;
} tables[] = {
    {"mdec_iq", mdec_iq, DW1_PRESS_MDEC_IQ_OFFSET, sizeof(mdec_iq)},
    {"mdec_coef", mdec_coef, DW1_PRESS_MDEC_COEF_OFFSET, sizeof(mdec_coef)},
    {"data_2", data_2, DW1_PRESS_DATA_2_OFFSET, sizeof(data_2)},
};
const void *libpress_table(const char *name, size_t *size)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!strcmp(name, tables[i].name)) {
            *size = tables[i].size;
            return tables[i].data;
        }
    }
    *size = 0;
    return NULL;
}

int libpress_set_table(const char *name, const void *data, size_t size)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!strcmp(name, tables[i].name) && size == tables[i].size && data) {
            memcpy(tables[i].data, data, size);
            return 0;
        }
    }
    return -1;
}

int libpress_load_tables(char *err, size_t errlen)
{
    const PackEntry *exe = cdrom_find_path(PACK_EXE_PATH);
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        const struct Table *t = &tables[i];
        if (!exe || cdrom_read_file(exe, t->offset, t->data, t->size) != 0) {
            snprintf(err, errlen, "libpress: cannot load %s from the Pack executable (missing or short)", t->name);
            for (unsigned j = 0; j < sizeof(tables) / sizeof(tables[0]); j++)
                memset(tables[j].data, 0, tables[j].size);
            return -1;
        }
    }
    return 0;
}


static void (*out_callback)();
static int out_event;
static unsigned long *out_buffer;
static int out_words;
static uint32_t decode_command;

/* asm:asm/us/main/psyq/libpress.s MDEC_out, DMA1 completion. */
static void output_done(void *unused)
{
    (void)unused;
    out_event = 0;
    mdec_out(out_buffer, (size_t)out_words);
    if (out_callback) out_callback();
}

/* asm:asm/us/main/psyq/libpress.s DecDCTReset / MDEC_reset. */
void DecDCTReset(int mode)
{
    clock_psyq_entry();
    if (mode != 0 && mode != 1) return;
    clock_cancel(out_event);
    out_event = 0;
    mdec_reset();
    if (mode == 0) {
        ResetCallback();
        out_callback = NULL;
        uint32_t command;
        memcpy(&command, mdec_iq, 4);
        PORT_ASSERT(mdec_command(command, mdec_iq + 4, 32) == 0);
        memcpy(&command, mdec_coef, 4);
        PORT_ASSERT(mdec_command(command, mdec_coef + 4, 32) == 0);
    }
}

/* asm:asm/us/main/psyq/libpress.s DecDCTin / MDEC_in. */
void DecDCTin(unsigned long *buf, int mode)
{
    clock_psyq_entry();
    buf[0] = (buf[0] & ~0x0a000000ul) |
        ((mode & 1) ? 0 : 0x08000000ul) | ((mode & 2) ? 0x02000000ul : 0);
    decode_command = buf[0];
    PORT_ASSERT(mdec_command(decode_command, buf + 1, buf[0] & 65535) == 0);
}

static int output_idle(void *unused) { (void)unused; return out_event == 0; }

/* asm:asm/us/main/psyq/libpress.s DecDCTout / MDEC_out.
 * DMA completion is an AD-8 event. A callback may enqueue the next slice;
 * the clock masks event dispatch until the current callback has returned. */
void DecDCTout(unsigned long *buf, int size)
{
    clock_psyq_entry();
    PORT_ASSERT(size >= 0 && (!size || buf));
    if (out_event) {
        PORT_ASSERT(!clock_in_handler());
        clock_wait_until(output_idle, NULL);
    }
    out_buffer = buf;
    out_words = size;
    unsigned mb_words = ((decode_command >> 27) & 3) == 2 ? 192 : 128;
    uint64_t count = ((unsigned)size + mb_words - 1) / mb_words;
    out_event = clock_schedule((count ? count : 1) * CLOCK_MDEC_MACROBLOCK_CYCLES,
                               output_done, NULL);
}

/* asm:asm/us/main/psyq/libpress.s DecDCToutCallback / DMACallback(1). */
int DecDCToutCallback(void (*func)())
{
    clock_psyq_entry();
    void (*previous)() = out_callback;
    out_callback = func;
    return (int)(uintptr_t)previous;
}

/* asm:asm/us/main/psyq/libpress.s DecDCTvlcBuild.
 * LZ byte expansion followed by a four-halfword XOR predictor. */
void DecDCTvlcBuild(unsigned short *table)
{
    clock_psyq_entry();
    uint8_t *dst = (uint8_t *)table;
    size_t src = 0, pos = 0;
    unsigned distance = 0;
    PORT_ASSERT(table);
    for (;;) {
        PORT_ASSERT(src < sizeof(data_2));
        unsigned code = data_2[src++];
        if (code >= 0xf0) {
            distance = 0;
            if (code != 0xf0) {
                PORT_ASSERT(src < sizeof(data_2));
                distance = ((code << 8) | data_2[src++]) - 0xf0ff;
            }
            if (distance == 0xf00) break;
        } else {
            size_t count = code + 1;
            PORT_ASSERT(pos + count <= 0x11000);
            for (size_t i = 0; i < count; i++) {
                if (distance) {
                    PORT_ASSERT(distance <= pos);
                    dst[pos] = dst[pos - distance];
                } else {
                    PORT_ASSERT(src < sizeof(data_2));
                    dst[pos] = data_2[src++];
                }
                pos++;
            }
        }
    }
    PORT_ASSERT(pos == 0x11000);
    for (unsigned i = 4; i < 0x8800; i++) table[i] ^= table[i - 4];
}

static uint16_t halfword(const void *p)
{
    const uint8_t *b = p;
    return (uint16_t)(b[0] | (unsigned)b[1] << 8);
}
static uint32_t table_word(const uint8_t *p)
{
    return halfword(p) | (uint32_t)halfword(p + 2) << 16;
}

typedef struct {
    const uint8_t *next;
    uint16_t *out;
    uint32_t bits, used, quant, block;
    uint32_t dc[3];
} VlcState;
static VlcState vlc;

/* asm:asm/us/main/psyq/libpress.s DecDCTvlc2 refill sequences. */
static void consume(unsigned n)
{
    vlc.bits <<= n & 31;
    vlc.used += n;
    if (vlc.used & 16) {
        vlc.used &= 15;
        vlc.bits |= (uint32_t)halfword(vlc.next) << vlc.used;
        vlc.next += 2;
    }
}

/* asm:asm/us/main/psyq/libpress.s DecDCTvlc2 (0x800E9884).
 * The asm has no version rejection: values <3 take the v2 path, >=3 the
 * differential DC path. Header/magic are copied, not validated. break_cnt
 * defaults to 0xffffff halfwords; the unreachable-in-DW1 continuation is retained. */
int DecDCTvlc2(unsigned long *bs, unsigned long *buf, unsigned short *table)
{
    clock_psyq_entry();
    const uint8_t *base = (const uint8_t *)table + 0x800;
    uintptr_t limit;
    if (bs) {
        const uint8_t *src = (const uint8_t *)bs;
        memset(&vlc, 0, sizeof(vlc));
        memcpy(buf, bs, 4);
        vlc.out = (uint16_t *)(buf + 1);
        vlc.quant = (uint32_t)halfword(src + 4) << 10;
        vlc.block = halfword(src + 6) >= 3 ? 1 : 0;
        vlc.bits = (uint32_t)halfword(src + 8) << 16 | halfword(src + 10);
        vlc.next = src + 12;
    }
    limit = (uintptr_t)vlc.out - 2u + 0x1fffffeu;
    if (!bs) goto ac;
    for (;;) {
        unsigned dc = vlc.bits >> 22;
        if (dc == (vlc.block ? 0x3ffu : 0x1ffu)) break;
        if (!vlc.block) {
            consume(10);
        } else {
            const uint8_t *entry = base - (vlc.block < 3 ? 0x400 : 0x800) + (vlc.bits >> 24) * 4;
            unsigned prefix = halfword(entry), n = halfword(entry + 2);
            consume(prefix);
            int delta = 0;
            if (n) {
                delta = vlc.bits >> (32 - n);
                if (!(vlc.bits & 0x80000000u)) delta -= (1u << n) - 1;
                consume(n);
            }
            unsigned channel = vlc.block < 3 ? vlc.block - 1 : 2;
            vlc.dc[channel] += (uint32_t)delta;
            dc = (vlc.dc[channel] * 4) & 1023;
            if (++vlc.block == 7) vlc.block = 1;
        }
        *vlc.out++ = (uint16_t)(vlc.quant | dc);
        if ((int32_t)((uintptr_t)(vlc.out - 1) - limit) >= 0) return 1;
ac:;
        for (;;) {
            unsigned index = vlc.bits >> 19;
            uint32_t entry = table_word(base + index * 8), extra = 0;
            if (!entry) {
                consume(8);
                entry = table_word(base + 0x10000 + (vlc.bits >> 23) * 4);
            } else extra = table_word(base + index * 8 + 4);
            consume(entry & 255);
            uint16_t codes[3] = {entry >> 16, extra & 65535, extra >> 16};
            int eob = 0;
            for (unsigned i = 0; i < 3; i++) {
                unsigned code = codes[i];
                if (i && !code) break;
                if (code == 0x7c1f) {
                    code = vlc.bits >> 16;
                    consume(16);
                }
                *vlc.out++ = code;
                if (code == 0xfe00) { eob = 1; break; }
            }
            if (eob) break;
        }
    }
    /* The delay slot makes this 65 padding halfwords, not 64. */
    for (unsigned i = 0; i < 65; i++) *vlc.out++ = 0xfe00;
    return 0;
}
