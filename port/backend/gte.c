/* Adapted from DW2-PC (CC0): psyq/gte.c @ 816ed0d */
#include <string.h>

#include "gte.h"
#include "../psyq/port.h"

/* Hardware rules: https://psx-spx.consoledev.net/geometrytransformationenginegte/.
 * Only DW1's commands are modeled. Inline operations do not advance the virtual clock
 * (negligible cost is a hypothesis to validate in Epic 3). */

typedef int64_t s64;
typedef uint64_t u64;

static int32_t d_[32] = { [GTE_LZCR] = 32 }; /* data registers, as stored */
static int32_t c_[32]; /* control registers, as stored */

/* FLAG bits. */
#define F_MAC_POS(i) (1u << (31 - (i))) /* MAC1..3 larger than 43 bits: 30, 29, 28 */
#define F_MAC_NEG(i) (1u << (28 - (i))) /* MAC1..3 smaller: 27, 26, 25 */
#define F_IR(i) (1u << (25 - (i)))      /* IR1..3 saturated: 24, 23, 22 */
#define F_COLOR(i) (1u << (22 - (i)))   /* colour FIFO R, G, B saturated: 21, 20, 19 */
#define F_SZ 0x00040000u               /* SZ3 / OTZ saturated */
#define F_DIV 0x00020000u              /* divide overflow */
#define F_MAC0_POS 0x00010000u
#define F_MAC0_NEG 0x00008000u
#define F_SX2 0x00004000u
#define F_SY2 0x00002000u
#define F_IR0 0x00001000u
#define F_ERROR_BITS 0x7F87E000u /* bits 30..23 and 18..13 set bit 31 */

static uint32_t flag_;

static int16_t lo16(int32_t v) { return (int16_t)(uint16_t)v; }
static int16_t hi16(int32_t v) { return (int16_t)(uint16_t)((uint32_t)v >> 16); }

static int16_t vx(int n) { return lo16(d_[GTE_VXY0 + n * 2]); }
static int16_t vy(int n) { return hi16(d_[GTE_VXY0 + n * 2]); }
static int16_t vz(int n) { return lo16(d_[GTE_VZ0 + n * 2]); }

/* Matrix element (row r, column c) of the control matrix at base 0 (RT), 8 (LLM) or 16 (LCM):
 * five words of packed s16 pairs, the 9th element alone in the low half of word 4. */
static int16_t mat(int base, int r, int c) {
    int k = r * 3 + c;
    int32_t w = c_[base + k / 2];
    return (k & 1) ? hi16(w) : lo16(w);
}

static s64 sx44(s64 v) { return (s64)((u64)v << 20) >> 20; }

/* MAC1..3 accumulation step: flags a 44-bit overflow, then wraps to 44 bits like the
 * hardware adder. */
static s64 mac_step(int i, s64 v) {
    if (v > 0x7FFFFFFFFFFLL) flag_ |= F_MAC_POS(i);
    else if (v < -0x80000000000LL) flag_ |= F_MAC_NEG(i);
    return sx44(v);
}

static void set_mac(int i, s64 v, int shift) {
    v = mac_step(i, v);
    d_[GTE_MAC0 + i] = (int32_t)(v >> shift);
}

static void set_mac0(s64 v) {
    if (v > 0x7FFFFFFFLL) flag_ |= F_MAC0_POS;
    else if (v < -0x80000000LL) flag_ |= F_MAC0_NEG;
    d_[GTE_MAC0] = (int32_t)v;
}

static void set_ir(int i, int32_t v, int lm) {
    int32_t lo = lm ? 0 : -0x8000;
    if (v < lo) { v = lo; flag_ |= F_IR(i); }
    else if (v > 0x7FFF) { v = 0x7FFF; flag_ |= F_IR(i); }
    d_[GTE_IR0 + i] = v;
}

static void set_mac_ir(int i, s64 v, int shift, int lm) {
    set_mac(i, v, shift);
    set_ir(i, d_[GTE_MAC0 + i], lm);
}

static void push_sz(s64 v) {
    if (v < 0) { v = 0; flag_ |= F_SZ; }
    else if (v > 0xFFFF) { v = 0xFFFF; flag_ |= F_SZ; }
    d_[GTE_SZ0] = d_[GTE_SZ1];
    d_[GTE_SZ1] = d_[GTE_SZ2];
    d_[GTE_SZ2] = d_[GTE_SZ3];
    d_[GTE_SZ3] = (int32_t)v;
}

static int32_t sat_sxy(int32_t v, uint32_t f) {
    if (v < -0x400) { v = -0x400; flag_ |= f; }
    else if (v > 0x3FF) { v = 0x3FF; flag_ |= f; }
    return v;
}

static void push_sxy(int32_t x, int32_t y) {
    d_[GTE_SXY0] = d_[GTE_SXY1];
    d_[GTE_SXY1] = d_[GTE_SXY2];
    d_[GTE_SXY2] = (int32_t)(((uint32_t)y << 16) | ((uint32_t)x & 0xFFFF));
}

static uint32_t sat_color(int i, int32_t v) {
    if (v < 0) { v = 0; flag_ |= F_COLOR(i); }
    else if (v > 0xFF) { v = 0xFF; flag_ |= F_COLOR(i); }
    return (uint32_t)v;
}

/* Colour FIFO push of MAC1..3 / 16 with RGBC's code byte. */
static void push_rgb_from_mac(void) {
    uint32_t r = sat_color(1, d_[GTE_MAC1] >> 4);
    uint32_t g = sat_color(2, d_[GTE_MAC2] >> 4);
    uint32_t b = sat_color(3, d_[GTE_MAC3] >> 4);
    d_[GTE_RGB0] = d_[GTE_RGB1];
    d_[GTE_RGB1] = d_[GTE_RGB2];
    d_[GTE_RGB2] = (int32_t)(r | g << 8 | b << 16 | ((uint32_t)d_[GTE_RGBC] & 0xFF000000u));
}

/* UNR reciprocal table (257 entries), as psx-spx gives it. */
static uint8_t unr_table_[0x101];
static int unr_ready_;

static void unr_init(void) {
    int i;
    for (i = 0; i < 0x101; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr_table_[i] = (uint8_t)(v < 0 ? 0 : v);
    }
    unr_ready_ = 1;
}

/* H / SZ3 in 16.16 as the GTE's Newton-Raphson divider computes it, or 0x1FFFF and the
 * divide overflow flag when H >= SZ3 * 2. */
static uint32_t unr_divide(uint32_t h, uint32_t sz3) {
    uint32_t z, n, d, u;
    if (h >= sz3 * 2) {
        flag_ |= F_DIV;
        return 0x1FFFF;
    }
    if (!unr_ready_) unr_init();
    z = 0;
    while (!((sz3 << z) & 0x8000)) z++;
    n = h << z;
    d = sz3 << z;
    u = unr_table_[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    {
        u64 q = ((u64)n * d + 0x8000) >> 16;
        return q > 0x1FFFF ? 0x1FFFF : (uint32_t)q;
    }
}

static void op_rtps(int vertex, int sf, int lm, int depth_cue) {
    int shift = sf ? 12 : 0;
    s64 m[3];
    int i;
    uint32_t q;
    s64 s;
    for (i = 0; i < 3; i++) {
        s64 v = (s64)c_[GTE_TRX + i] * 4096;
        v = mac_step(i + 1, v + (s64)mat(0, i, 0) * vx(vertex));
        v = mac_step(i + 1, v + (s64)mat(0, i, 1) * vy(vertex));
        v = mac_step(i + 1, v + (s64)mat(0, i, 2) * vz(vertex));
        m[i] = v;
        set_mac(i + 1, v, shift);
    }
    set_ir(1, d_[GTE_MAC1], lm);
    set_ir(2, d_[GTE_MAC2], lm);
    /* IR3: saturated from MAC3, but its flag comes from MAC3's value shifted by 12 whatever sf
     * says (hardware quirk). */
    {
        s64 z12 = m[2] >> 12;
        int32_t v = d_[GTE_MAC3];
        int32_t lo = lm ? 0 : -0x8000;
        if (z12 < -0x8000 || z12 > 0x7FFF) flag_ |= F_IR(3);
        if (v < lo) v = lo;
        else if (v > 0x7FFF) v = 0x7FFF;
        d_[GTE_IR3] = v;
    }
    push_sz(m[2] >> 12);
    q = unr_divide((uint16_t)c_[GTE_H], (uint32_t)d_[GTE_SZ3]);
    s = (s64)q * (int16_t)d_[GTE_IR1] + c_[GTE_OFX];
    set_mac0(s);
    {
        int32_t x = sat_sxy((int32_t)(s >> 16), F_SX2);
        s = (s64)q * (int16_t)d_[GTE_IR2] + c_[GTE_OFY];
        set_mac0(s);
        push_sxy(x, sat_sxy((int32_t)(s >> 16), F_SY2));
    }
    /* RTPT updates depth cueing only for V2, including its overflow flags. */
    if (!depth_cue) return;
    s = (s64)q * (int16_t)c_[GTE_DQA] + c_[GTE_DQB];
    set_mac0(s);
    {
        int32_t ir0 = (int32_t)(s >> 12);
        if (ir0 < 0) { ir0 = 0; flag_ |= F_IR0; }
        else if (ir0 > 0x1000) { ir0 = 0x1000; flag_ |= F_IR0; }
        d_[GTE_IR0] = ir0;
    }
}

static void op_nclip(void) {
    s64 x0 = lo16(d_[GTE_SXY0]), y0 = hi16(d_[GTE_SXY0]);
    s64 x1 = lo16(d_[GTE_SXY1]), y1 = hi16(d_[GTE_SXY1]);
    s64 x2 = lo16(d_[GTE_SXY2]), y2 = hi16(d_[GTE_SXY2]);
    set_mac0(x0 * y1 + x1 * y2 + x2 * y0 - x0 * y2 - x1 * y0 - x2 * y1);
}

/* MAC/IR = (T << 12 + M * V) >> shift, one adder step per term. t == NULL: no translation. */
static void mul_mat_vec(int base, const int32_t *t, int16_t x, int16_t y, int16_t z, int shift,
                        int lm) {
    int i;
    for (i = 0; i < 3; i++) {
        s64 v = t ? (s64)t[i] * 4096 : 0;
        v = mac_step(i + 1, v + (s64)mat(base, i, 0) * x);
        v = mac_step(i + 1, v + (s64)mat(base, i, 1) * y);
        v = mac_step(i + 1, v + (s64)mat(base, i, 2) * z);
        set_mac_ir(i + 1, v, shift, lm);
    }
}

static void op_mvmva(uint32_t op, int sf, int lm) {
    int shift = sf ? 12 : 0;
    int mx = (op >> 17) & 3, vsel = (op >> 15) & 3, cv = (op >> 13) & 3;
    int16_t x, y, z;
    int32_t t[3];
    const int32_t *tp = t;
    static const int base_of[3] = { GTE_RT0, GTE_LLM0, GTE_LCM0 };
    if (vsel == 3) {
        x = (int16_t)d_[GTE_IR1]; y = (int16_t)d_[GTE_IR2]; z = (int16_t)d_[GTE_IR3];
    } else {
        x = vx(vsel); y = vy(vsel); z = vz(vsel);
    }
    if (cv == 3) tp = NULL;
    else {
        int tb = cv == 0 ? GTE_TRX : cv == 1 ? GTE_RBK : GTE_RFC;
        t[0] = c_[tb]; t[1] = c_[tb + 1]; t[2] = c_[tb + 2];
    }
    PORT_ASSERT(mx != 3 && cv != 2);
    if (mx == 3 || cv == 2) return;
    mul_mat_vec(base_of[mx], tp, x, y, z, shift, lm);
}

static void op_nccs(int vertex, int sf, int lm) {
    int shift = sf ? 12 : 0;
    int32_t bk[3];
    bk[0] = c_[GTE_RBK]; bk[1] = c_[GTE_GBK]; bk[2] = c_[GTE_BBK];
    mul_mat_vec(GTE_LLM0, NULL, vx(vertex), vy(vertex), vz(vertex), shift, lm);
    mul_mat_vec(GTE_LCM0, bk, (int16_t)d_[GTE_IR1], (int16_t)d_[GTE_IR2], (int16_t)d_[GTE_IR3],
                shift, lm);
    for (int i = 1; i <= 3; i++) {
        uint32_t color = ((uint32_t)d_[GTE_RGBC] >> ((i - 1) * 8)) & 0xFF;
        set_mac_ir(i, (s64)color * d_[GTE_IR0 + i] * 16, shift, lm);
    }
    push_rgb_from_mac();
}

static void op_avsz(int count) {
    s64 sum = 0;
    for (int i = 4 - count; i < 4; i++) sum += d_[GTE_SZ0 + i];
    s64 value = sum * c_[count == 3 ? GTE_ZSF3 : GTE_ZSF4];
    set_mac0(value);
    /* OTZ uses the full intermediate, not the wrapped 32-bit MAC0. */
    value >>= 12;
    if (value < 0) { value = 0; flag_ |= F_SZ; }
    else if (value > 0xFFFF) { value = 0xFFFF; flag_ |= F_SZ; }
    d_[GTE_OTZ] = (int32_t)value;
}

void gte_command(uint32_t op) {
    int sf = (op >> 19) & 1, lm = (op >> 10) & 1;
    flag_ = 0;
    switch (op & 0x3F) {
    case 0x01: op_rtps(0, sf, lm, 1); break;
    case 0x30:
        for (int i = 0; i < 3; i++) op_rtps(i, sf, lm, i == 2);
        break;
    case 0x06: op_nclip(); break;
    case 0x12: op_mvmva(op, sf, lm); break;
    case 0x1B: op_nccs(0, sf, lm); break;
    case 0x3F:
        for (int i = 0; i < 3; i++) op_nccs(i, sf, lm);
        break;
    case 0x2D: op_avsz(3); break;
    case 0x2E: op_avsz(4); break;
    default: PORT_ASSERT(0 && "GTE command not modeled"); break;
    }
    if (flag_ & F_ERROR_BITS) flag_ |= 0x80000000u;
    c_[GTE_FLAG] = (int32_t)flag_;
}

void gte_reset(void) {
    memset(d_, 0, sizeof(d_));
    memset(c_, 0, sizeof(c_));
    d_[GTE_LZCR] = 32;
    flag_ = 0;
}

static int32_t lzc(int32_t v) {
    uint32_t u = v < 0 ? ~(uint32_t)v : (uint32_t)v;
    int32_t n = 0;
    while (n < 32 && !(u & 0x80000000u)) { u <<= 1; n++; }
    return n;
}

static uint32_t sat5(int32_t ir) {
    int32_t v = ir >> 7;
    return (uint32_t)(v < 0 ? 0 : v > 0x1F ? 0x1F : v);
}

void gte_mtc2(int reg, uint32_t value) {
    switch (reg) {
    case GTE_VZ0: case GTE_VZ1: case GTE_VZ2:
    case GTE_IR0: case GTE_IR1: case GTE_IR2: case GTE_IR3:
        d_[reg] = lo16((int32_t)value);
        break;
    case GTE_OTZ: case GTE_SZ0: case GTE_SZ1: case GTE_SZ2: case GTE_SZ3:
        d_[reg] = (int32_t)(value & 0xFFFF);
        break;
    case GTE_SXYP:
        d_[GTE_SXY0] = d_[GTE_SXY1];
        d_[GTE_SXY1] = d_[GTE_SXY2];
        d_[GTE_SXY2] = (int32_t)value;
        break;
    case GTE_IRGB:
        d_[GTE_IR1] = (int32_t)((value & 0x1F) << 7);
        d_[GTE_IR2] = (int32_t)(((value >> 5) & 0x1F) << 7);
        d_[GTE_IR3] = (int32_t)(((value >> 10) & 0x1F) << 7);
        break;
    case GTE_ORGB: case GTE_LZCR:
        break; /* read only */
    case GTE_LZCS:
        d_[GTE_LZCS] = (int32_t)value;
        d_[GTE_LZCR] = lzc((int32_t)value);
        break;
    default:
        d_[reg] = (int32_t)value;
        break;
    }
}

uint32_t gte_mfc2(int reg) {
    switch (reg) {
    case GTE_SXYP:
        return (uint32_t)d_[GTE_SXY2];
    case GTE_IRGB: case GTE_ORGB:
        return sat5(d_[GTE_IR1]) | sat5(d_[GTE_IR2]) << 5 | sat5(d_[GTE_IR3]) << 10;
    default:
        return (uint32_t)d_[reg];
    }
}

void gte_ctc2(int reg, uint32_t value) {
    switch (reg) {
    case 4: case 12: case 20: /* RT33, L33, LB3 */
    case GTE_H: case GTE_DQA: case GTE_ZSF3: case GTE_ZSF4:
        c_[reg] = lo16((int32_t)value); /* H reads back sign extended too */
        break;
    case GTE_FLAG:
        value &= 0x7FFFF000u;
        if (value & F_ERROR_BITS) value |= 0x80000000u;
        c_[GTE_FLAG] = (int32_t)value;
        break;
    default:
        c_[reg] = (int32_t)value;
        break;
    }
}

uint32_t gte_cfc2(int reg) { return (uint32_t)c_[reg]; }

static void gte_lwc2(int reg, const void *addr) {
    uint32_t w;
    memcpy(&w, addr, 4);
    gte_mtc2(reg, w);
}

static void gte_swc2(int reg, void *addr) {
    uint32_t w = gte_mfc2(reg);
    memcpy(addr, &w, 4);
}

/* Register sequences from include/gcc/inline_n.h; memcpy permits unaligned operands. */
static void load_vertex(int vertex, const void *src) {
    gte_lwc2(GTE_VXY0 + vertex * 2, src);
    gte_lwc2(GTE_VZ0 + vertex * 2, (const char *)src + 4);
}

void port_gte_ldv0(const void *v0) { load_vertex(0, v0); }
void port_gte_ldv3(const void *v0, const void *v1, const void *v2) {
    load_vertex(0, v0);
    load_vertex(1, v1);
    load_vertex(2, v2);
}

void port_gte_rtps(void) { gte_command(GTE_CMD_RTPS); }
void port_gte_rtpt(void) { gte_command(GTE_CMD_RTPT); }
void port_gte_stsxy(void *sxy) { gte_swc2(GTE_SXY2, sxy); }
void port_gte_stsxy3(void *sxy0, void *sxy1, void *sxy2) {
    gte_swc2(GTE_SXY0, sxy0);
    gte_swc2(GTE_SXY1, sxy1);
    gte_swc2(GTE_SXY2, sxy2);
}
void port_gte_stszotz(void *otz) {
    int32_t value = (int32_t)gte_mfc2(GTE_SZ3) >> 2;
    memcpy(otz, &value, sizeof(value));
}
void port_gte_ldlzc(long value) { gte_mtc2(GTE_LZCS, (uint32_t)value); }
void port_gte_stlzc(void *lzc) { gte_swc2(GTE_LZCR, lzc); }

static void set_ctrl_words(int reg, const void *src, int count) {
    for (int i = 0; i < count; i++) {
        uint32_t value;
        memcpy(&value, (const char *)src + i * 4, sizeof(value));
        gte_ctc2(reg + i, value);
    }
}
void port_gte_SetRotMatrix(const void *m) { set_ctrl_words(GTE_RT0, m, 5); }
void port_gte_SetTransMatrix(const void *m) { set_ctrl_words(GTE_TRX, (const char *)m + 20, 3); }
