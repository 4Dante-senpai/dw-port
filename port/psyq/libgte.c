/* libgte entries translated from asm:asm/us/main/psyq/libgte.s.
 * All arithmetic shifts use the signed 32-bit view; unsigned arithmetic preserves MIPS
 * low-word wrap without C signed overflow. No executable or table bytes are embedded. */
#include "psyq.h"
#include "clock.h"
#include "libgte.h"
#include "port.h"
#include "../backend/gte.h"
#include "../backend/cdrom.h"
#include "dw1_gte_tables.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(long) == 4 && sizeof(MATRIX) == 32 && offsetof(MATRIX, t) == 20,
               "PS1 geometry layout");
_Static_assert(sizeof(VECTOR) == 16 && sizeof(SVECTOR) == 8 && sizeof(CVECTOR) == 4,
               "PS1 vector layout");
static uint8_t rsin_tbl[DW1_GTE_RSIN_TBL_SIZE];
static uint8_t sqrt_tbl[DW1_GTE_SQRT_SIZE];
static uint8_t rcossin_tbl[DW1_GTE_RCOSSIN_TBL_SIZE];
static uint8_t ratan_tbl[DW1_GTE_RATAN_TBL_SIZE];
static const struct Table {
    const char *name;
    uint8_t *data;
    uint32_t offset, size;
} tables[] = {
    {"rsin_tbl", rsin_tbl, DW1_GTE_RSIN_TBL_OFFSET, sizeof(rsin_tbl)},
    {"SQRT", sqrt_tbl, DW1_GTE_SQRT_OFFSET, sizeof(sqrt_tbl)},
    {"rcossin_tbl", rcossin_tbl, DW1_GTE_RCOSSIN_TBL_OFFSET, sizeof(rcossin_tbl)},
    {"ratan_tbl", ratan_tbl, DW1_GTE_RATAN_TBL_OFFSET, sizeof(ratan_tbl)}
};
static uint32_t matrix_stack[20][8];
static unsigned matrix_sp;
/* The asm SAVERA words only preserve MIPS return addresses across calls; C calls preserve
 * those automatically. MSP/MSTACK are the persistent state above. */

const void *libgte_table(const char *name, size_t *size)
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

int libgte_set_table(const char *name, const void *data, size_t size)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!strcmp(name, tables[i].name) && size == tables[i].size && data) {
            memcpy(tables[i].data, data, size);
            return 0;
        }
    }
    return -1;
}

int libgte_load_tables(char *err, size_t errlen)
{
    const PackEntry *exe = cdrom_find_path(PACK_EXE_PATH);
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        const struct Table *t = &tables[i];
        if (!exe || cdrom_read_file(exe, t->offset, t->data, t->size) != 0) {
            snprintf(err, errlen, "libgte: cannot load %s from the Pack executable (missing or short)", t->name);
            for (unsigned j = 0; j < sizeof(tables) / sizeof(tables[0]); j++)
                memset(tables[j].data, 0, tables[j].size);
            return -1;
        }
    }
    return 0;
}

static int32_t half(const uint8_t *table, unsigned index)
{
    return (int16_t)((uint16_t)table[2 * index] | (uint16_t)table[2 * index + 1] << 8);
}
static uint32_t word(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}
static void put_word(void *p, uint32_t v) { memcpy(p, &v, sizeof(v)); }
static uint32_t pair(int32_t x, int32_t y) { return (uint16_t)x | (uint32_t)(uint16_t)y << 16; }
static int32_t sar(uint32_t x, unsigned n) { return (int32_t)x >> n; }
static int32_t mul12(int32_t x, int32_t y) { return sar((uint32_t)x * (uint32_t)y, 12); }

/* asm:asm/us/main/psyq/libgte.s sin_1 (internal, no public PsyQ entry). */
static int sin_1(unsigned a)
{
    if (a <= 1024) return half(rsin_tbl, a);
    if (a <= 2048) return half(rsin_tbl, 2048 - a);
    if (a <= 3072) return -half(rsin_tbl, a - 2048);
    return -half(rsin_tbl, 4096 - a);
}

/* asm:asm/us/main/psyq/libgte.s rsin */
int rsin(int a)
{
    clock_psyq_entry();
    unsigned magnitude = a < 0 ? 0u - (uint32_t)a : (uint32_t)a;
    int s = sin_1(magnitude & 4095);
    return a < 0 ? -s : s;
}

/* asm:asm/us/main/psyq/libgte.s rcos */
int rcos(int a)
{
    clock_psyq_entry();
    unsigned v = (a < 0 ? 0u - (uint32_t)a : (uint32_t)a) & 4095;
    if (v <= 1024) return half(rsin_tbl, 1024 - v);
    if (v <= 2048) return -half(rsin_tbl, v - 1024);
    if (v <= 3072) return -half(rsin_tbl, 3072 - v);
    return half(rsin_tbl, v - 3072);
}

/* asm:asm/us/main/psyq/libgte.s SquareRoot0 */
long SquareRoot0(long a)
{
    clock_psyq_entry();
    gte_mtc2(GTE_LZCS, (uint32_t)a);
    unsigned leading = gte_mfc2(GTE_LZCR);
    if (leading == 32) return 0;
    unsigned even = leading & ~1u;
    uint32_t scaled = even >= 24 ? (uint32_t)a << (even - 24) : (uint32_t)(a >> (24 - even));
    unsigned index = scaled - 64;
    PORT_ASSERT(index < sizeof(sqrt_tbl) / 2);
    return ((uint32_t)half(sqrt_tbl, index) << ((31 - even) / 2)) >> 12;
}

/* asm:asm/us/main/psyq/libgte.s ratan2 */
long ratan2(long y, long x)
{
    clock_psyq_entry();
    int32_t ax = (int32_t)(x < 0 ? 0u - (uint32_t)x : (uint32_t)x);
    int32_t ay = (int32_t)(y < 0 ? 0u - (uint32_t)y : (uint32_t)y);
    if (ax == 0 && ay == 0) return 0;
    int swap = ay >= ax;
    int32_t small = swap ? ax : ay, large = swap ? ay : ax;
    int32_t numerator = (small & 0x7fe00000) ? small : (int32_t)((uint32_t)small << 10);
    int32_t denominator = (small & 0x7fe00000) ? large >> 10 : large;
    PORT_ASSERT(denominator != 0 && !(numerator == INT32_MIN && denominator == -1));
    int32_t index = numerator / denominator;
    PORT_ASSERT(index >= 0 && (unsigned)index < sizeof(ratan_tbl) / 2);
    int32_t angle = half(ratan_tbl, (unsigned)index);
    if (swap) angle = 1024 - angle;
    if (x < 0) angle = 2048 - angle;
    return y < 0 ? -angle : angle;
}

static void load_matrix(const MATRIX *m, int base)
{
    for (int i = 0; i < 5; i++)
        gte_ctc2(base + i, word((const uint8_t *)m + 4 * i));
}

/* asm:asm/us/main/psyq/libgte.s SetRotMatrix */
void SetRotMatrix(MATRIX *m) { clock_psyq_entry(); load_matrix(m, GTE_RT0); }
/* asm:asm/us/main/psyq/libgte.s SetLightMatrix */
void SetLightMatrix(MATRIX *m) { clock_psyq_entry(); load_matrix(m, GTE_LLM0); }
/* asm:asm/us/main/psyq/libgte.s SetTransMatrix */
void SetTransMatrix(MATRIX *m)
{
    clock_psyq_entry();
    for (int i = 0; i < 3; i++) gte_ctc2(GTE_TRX + i, (uint32_t)m->t[i]);
}

/* asm:asm/us/main/psyq/libgte.s PushMatrix */
void PushMatrix(void)
{
    clock_psyq_entry();
    if (matrix_sp == 20) { printf("libgte: matrix stack is full\n"); return; }
    for (int i = 0; i < 8; i++) matrix_stack[matrix_sp][i] = gte_cfc2(i);
    matrix_sp++;
}
/* asm:asm/us/main/psyq/libgte.s PopMatrix */
void PopMatrix(void)
{
    clock_psyq_entry();
    if (matrix_sp == 0) { printf("libgte: matrix stack is empty\n"); return; }
    matrix_sp--;
    for (int i = 0; i < 8; i++) gte_ctc2(i, matrix_stack[matrix_sp][i]);
}

/* asm:asm/us/main/psyq/libgte.s ApplyMatrix */
VECTOR *ApplyMatrix(MATRIX *m, SVECTOR *v, VECTOR *out)
{
    clock_psyq_entry();
    load_matrix(m, GTE_RT0);
    port_gte_ldv0(v);
    gte_command(GTE_CMD_MVMVA(1, 0, 0, 3, 0));
    out->vx = (int32_t)gte_mfc2(GTE_MAC1);
    out->vy = (int32_t)gte_mfc2(GTE_MAC2);
    out->vz = (int32_t)gte_mfc2(GTE_MAC3);
    return out;
}
/* asm:asm/us/main/psyq/libgte.s ApplyMatrixSV */
SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v, SVECTOR *out)
{
    clock_psyq_entry();
    load_matrix(m, GTE_RT0);
    port_gte_ldv0(v);
    gte_command(GTE_CMD_MVMVA(1, 0, 0, 3, 0));
    out->vx = (int16_t)gte_mfc2(GTE_IR1);
    out->vy = (int16_t)gte_mfc2(GTE_IR2);
    out->vz = (int16_t)gte_mfc2(GTE_IR3);
    return out;
}

static VECTOR *apply_long(VECTOR *v, VECTOR *out, int transpose)
{
    int32_t input[3] = {v->vx, v->vy, v->vz}, high[3], low[3];
    uint32_t mac[3];
    for (int i = 0; i < 3; i++) {
        uint32_t magnitude = input[i] < 0 ? 0u - (uint32_t)input[i] : (uint32_t)input[i];
        high[i] = sar(magnitude, 15);
        low[i] = (int32_t)(magnitude & 0x7fff);
        if (input[i] < 0) {
            high[i] = (int32_t)(0u - (uint32_t)high[i]);
            low[i] = -low[i];
        }
    }
    if (transpose) {
        gte_mtc2(GTE_VXY0, pair(high[0], high[1]));
        gte_mtc2(GTE_VZ0, (uint32_t)high[2]);
    } else {
        for (int i = 0; i < 3; i++) gte_mtc2(GTE_IR1 + i, (uint32_t)high[i]);
    }
    gte_command(GTE_CMD_MVMVA(0, 0, transpose ? 0 : 3, 3, 0));
    for (int i = 0; i < 3; i++) mac[i] = gte_mfc2(GTE_MAC1 + i) << 3;
    if (transpose) {
        gte_mtc2(GTE_VXY0, pair(low[0], low[1]));
        gte_mtc2(GTE_VZ0, (uint32_t)low[2]);
    } else {
        for (int i = 0; i < 3; i++) gte_mtc2(GTE_IR1 + i, (uint32_t)low[i]);
    }
    gte_command(GTE_CMD_MVMVA(1, 0, transpose ? 0 : 3, 3, 0));
    out->vx = (int32_t)(gte_mfc2(GTE_MAC1) + mac[0]);
    out->vy = (int32_t)(gte_mfc2(GTE_MAC2) + mac[1]);
    out->vz = (int32_t)(gte_mfc2(GTE_MAC3) + mac[2]);
    return out;
}
/* asm:asm/us/main/psyq/libgte.s ApplyMatrixLV */
VECTOR *ApplyMatrixLV(MATRIX *m, VECTOR *v, VECTOR *out)
{
    clock_psyq_entry();
    load_matrix(m, GTE_RT0);
    return apply_long(v, out, 0);
}
/* asm:asm/us/main/psyq/libgte.s ApplyTransposeMatrixLV */
VECTOR *ApplyTransposeMatrixLV(MATRIX *m, VECTOR *v, VECTOR *out)
{
    clock_psyq_entry();
    gte_ctc2(0, pair(m->m[0][0], m->m[1][0]));
    gte_ctc2(1, pair(m->m[2][0], m->m[0][1]));
    gte_ctc2(2, pair(m->m[1][1], m->m[2][1]));
    gte_ctc2(3, pair(m->m[0][2], m->m[1][2]));
    gte_ctc2(4, word((uint8_t *)m + 16));
    return apply_long(v, out, 1);
}

static void multiply(MATRIX *a, MATRIX *b, MATRIX *out)
{
    int16_t result[3][3];
    load_matrix(a, GTE_RT0);
    for (int col = 0; col < 3; col++) {
        gte_mtc2(GTE_VXY0, pair(b->m[0][col], b->m[1][col]));
        gte_mtc2(GTE_VZ0, (uint32_t)b->m[2][col]);
        gte_command(GTE_CMD_MVMVA(1, 0, 0, 3, 0));
        for (int row = 0; row < 3; row++) result[row][col] = (int16_t)gte_mfc2(GTE_IR1 + row);
    }
    memcpy(out->m, result, sizeof(result));
    /* swc2 IR3 writes the last coefficient AND its sign extension into padding. */
    put_word((uint8_t *)out + 16, gte_mfc2(GTE_IR3));
}
/* asm:asm/us/main/psyq/libgte.s MulMatrix0 */
MATRIX *MulMatrix0(MATRIX *a, MATRIX *b, MATRIX *out)
{
    clock_psyq_entry(); multiply(a, b, out); return out;
}
/* asm:asm/us/main/psyq/libgte.s MulMatrix */
MATRIX *MulMatrix(MATRIX *a, MATRIX *b)
{
    clock_psyq_entry(); multiply(a, b, a); return a;
}
/* asm:asm/us/main/psyq/libgte.s CompMatrix */
MATRIX *CompMatrix(MATRIX *a, MATRIX *b, MATRIX *out)
{
    clock_psyq_entry();
    multiply(a, b, out);
    /* DW1 uses the low signed half of each translation component, not ApplyMatrixLV. */
    gte_mtc2(GTE_VXY0, pair(b->t[0], b->t[1]));
    gte_mtc2(GTE_VZ0, (uint32_t)b->t[2]);
    gte_command(GTE_CMD_MVMVA(1, 0, 0, 3, 0));
    for (int i = 0; i < 3; i++) out->t[i] = (int32_t)(gte_mfc2(GTE_MAC1 + i) + (uint32_t)a->t[i]);
    return out;
}
/* asm:asm/us/main/psyq/libgte.s TransMatrix */
MATRIX *TransMatrix(MATRIX *m, VECTOR *v)
{
    clock_psyq_entry();
    long x = v->vx, y = v->vy, z = v->vz;
    m->t[0] = x; m->t[1] = y; m->t[2] = z;
    return m;
}
/* asm:asm/us/main/psyq/libgte.s ScaleMatrix */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v)
{
    clock_psyq_entry();
    int32_t scale[3] = {v->vx, v->vy, v->vz};
    int32_t last = 0;
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            last = mul12(m->m[row][col], scale[col]);
            m->m[row][col] = (int16_t)last;
        }
    put_word((uint8_t *)m + 16, (uint32_t)last);
    return m;
}
/* asm:asm/us/main/psyq/libgte.s TransposeMatrix */
MATRIX *TransposeMatrix(MATRIX *a, MATRIX *out)
{
    clock_psyq_entry();
    /* Preserve instruction ordering, including the in-place case. */
    uint8_t *d = (uint8_t *)out;
    const uint8_t *s = (const uint8_t *)a;
    uint32_t t1 = word(s), t2 = word(s + 4);
    put_word(d + 4, t1); put_word(d, t2); out->m[0][0] = (int16_t)t1;
    uint32_t t3 = word(s + 8);
    t1 = word(s + 12);
    put_word(d + 12, t3); put_word(d + 8, t1);
    out->m[2][0] = (int16_t)t2; out->m[1][1] = (int16_t)t3;
    int16_t last = a->m[2][2];
    out->m[0][2] = (int16_t)t1; out->m[2][2] = last;
    return out;
}

static void sincos_table(int angle, int32_t *s, int32_t *c)
{
    unsigned index = (unsigned)(angle < 0 ? -angle : angle) & 4095;
    *s = half(rcossin_tbl, 2 * index);
    if (angle < 0) *s = -*s;
    *c = half(rcossin_tbl, 2 * index + 1);
}

/* asm:asm/us/main/psyq/libgte.s RotMatrix */
MATRIX *RotMatrix(SVECTOR *r, MATRIX *m)
{
    clock_psyq_entry();
    int32_t sx, cx, sy, cy, sz, cz;
    sincos_table(r->vx, &sx, &cx); sincos_table(r->vy, &sy, &cy); sincos_table(r->vz, &sz, &cz);
    m->m[0][2] = (int16_t)sy;
    m->m[1][2] = (int16_t)sar(0u - (uint32_t)cy * (uint32_t)sx, 12);
    m->m[2][2] = (int16_t)mul12(cy, cx);
    m->m[0][0] = (int16_t)mul12(cz, cy);
    m->m[0][1] = (int16_t)sar(0u - (uint32_t)sz * (uint32_t)cy, 12);
    int32_t t = mul12(cz, -sy);
    m->m[1][0] = (int16_t)(mul12(sz, cx) - mul12(t, sx));
    m->m[2][0] = (int16_t)(mul12(sz, sx) + mul12(t, cx));
    t = mul12(sz, -sy);
    m->m[1][1] = (int16_t)(mul12(cz, cx) + mul12(t, sx));
    m->m[2][1] = (int16_t)(mul12(cz, sx) - mul12(t, cx));
    return m;
}
/* asm:asm/us/main/psyq/libgte.s RotMatrixYXZ */
MATRIX *RotMatrixYXZ(SVECTOR *r, MATRIX *m)
{
    clock_psyq_entry();
    int32_t sx, cx, sy, cy, sz, cz;
    sincos_table(r->vx, &sx, &cx); sincos_table(r->vy, &sy, &cy); sincos_table(r->vz, &sz, &cz);
    m->m[1][2] = (int16_t)-sx;
    m->m[0][2] = (int16_t)mul12(sy, cx); m->m[2][2] = (int16_t)mul12(cy, cx);
    m->m[1][0] = (int16_t)mul12(sz, cx); m->m[1][1] = (int16_t)mul12(cz, cx);
    int32_t t = mul12(sy, sx);
    m->m[0][0] = (int16_t)(mul12(cy, cz) + mul12(t, sz));
    m->m[0][1] = (int16_t)(-mul12(cy, sz) + mul12(t, cz));
    t = mul12(cy, sx);
    m->m[2][1] = (int16_t)(mul12(sy, sz) + mul12(t, cz));
    m->m[2][0] = (int16_t)(-mul12(sy, cz) + mul12(t, sz));
    return m;
}
/* asm:asm/us/main/psyq/libgte.s RotMatrixZYX */
MATRIX *RotMatrixZYX(SVECTOR *r, MATRIX *m)
{
    clock_psyq_entry();
    int32_t sx, cx, sy, cy, sz, cz;
    sincos_table(r->vx, &sx, &cx); sincos_table(r->vy, &sy, &cy); sincos_table(r->vz, &sz, &cz);
    m->m[2][0] = (int16_t)-sy;
    m->m[2][1] = (int16_t)mul12(sx, cy); m->m[2][2] = (int16_t)mul12(cx, cy);
    m->m[0][0] = (int16_t)mul12(cy, cz); m->m[1][0] = (int16_t)mul12(sz, cy);
    int32_t t = mul12(sx, sy);
    m->m[0][1] = (int16_t)(mul12(t, cz) - mul12(sz, cx));
    m->m[1][1] = (int16_t)(mul12(t, sz) + mul12(cx, cz));
    t = mul12(sy, cx);
    m->m[0][2] = (int16_t)(mul12(t, cz) + mul12(sx, sz));
    m->m[1][2] = (int16_t)(mul12(t, sz) - mul12(sx, cz));
    return m;
}

/* asm:asm/us/main/psyq/libgte.s NormalColorCol */
void NormalColorCol(SVECTOR *v, CVECTOR *color, CVECTOR *out)
{
    clock_psyq_entry();
    port_gte_ldv0(v); gte_mtc2(GTE_RGBC, word(color));
    gte_command(GTE_CMD_NCCS); put_word(out, gte_mfc2(GTE_RGB2));
}
/* asm:asm/us/main/psyq/libgte.s NormalColorCol3 */
void NormalColorCol3(SVECTOR *a, SVECTOR *b, SVECTOR *c, CVECTOR *color,
                     CVECTOR *x, CVECTOR *y, CVECTOR *z)
{
    clock_psyq_entry();
    port_gte_ldv3(a, b, c); gte_mtc2(GTE_RGBC, word(color));
    gte_command(GTE_CMD_NCCT);
    put_word(x, gte_mfc2(GTE_RGB0)); put_word(y, gte_mfc2(GTE_RGB1)); put_word(z, gte_mfc2(GTE_RGB2));
}

/* asm:asm/us/main/psyq/libgte.s RotTransPers */
long RotTransPers(SVECTOR *v, long *sxy, long *p, long *flag)
{
    clock_psyq_entry();
    port_gte_ldv0(v); gte_command(GTE_CMD_RTPS);
    *sxy = (int32_t)gte_mfc2(GTE_SXY2); *p = (int32_t)gte_mfc2(GTE_IR0);
    *flag = (int32_t)gte_cfc2(GTE_FLAG);
    return gte_mfc2(GTE_SZ3) >> 2;
}
/* asm:asm/us/main/psyq/libgte.s RotTransPers3 */
long RotTransPers3(SVECTOR *a, SVECTOR *b, SVECTOR *c, long *x, long *y, long *z, long *p, long *flag)
{
    clock_psyq_entry();
    port_gte_ldv3(a, b, c); gte_command(GTE_CMD_RTPT);
    port_gte_stsxy3(x, y, z); *p = (int32_t)gte_mfc2(GTE_IR0);
    *flag = (int32_t)gte_cfc2(GTE_FLAG);
    return gte_mfc2(GTE_SZ3) >> 2;
}
/* asm:asm/us/main/psyq/libgte.s RotTransPers4 */
long RotTransPers4(SVECTOR *a, SVECTOR *b, SVECTOR *c, SVECTOR *d,
                   long *x, long *y, long *z, long *w, long *p, long *flag)
{
    clock_psyq_entry();
    port_gte_ldv3(a, b, c); gte_command(GTE_CMD_RTPT); port_gte_stsxy3(x, y, z);
    uint32_t f = gte_cfc2(GTE_FLAG);
    port_gte_ldv0(d); gte_command(GTE_CMD_RTPS);
    *w = (int32_t)gte_mfc2(GTE_SXY2); *p = (int32_t)gte_mfc2(GTE_IR0);
    *flag = (int32_t)(f | gte_cfc2(GTE_FLAG));
    return gte_mfc2(GTE_SZ3) >> 2;
}
/* asm:asm/us/main/psyq/libgte.s RotNclip3 */
long RotNclip3(SVECTOR *a, SVECTOR *b, SVECTOR *c,
               long *x, long *y, long *z, long *p, long *otz, long *flag)
{
    clock_psyq_entry();
    port_gte_ldv3(a, b, c); gte_command(GTE_CMD_RTPT);
    uint32_t f = gte_cfc2(GTE_FLAG);
    gte_command(GTE_CMD_NCLIP); *flag = (int32_t)f;
    int32_t area = (int32_t)gte_mfc2(GTE_MAC0);
    if (area > 0) {
        port_gte_stsxy3(x, y, z); *p = (int32_t)gte_mfc2(GTE_IR0); *otz = gte_mfc2(GTE_SZ3) >> 2;
    }
    return area;
}
/* asm:asm/us/main/psyq/libgte.s RotNclip4 */
long RotNclip4(SVECTOR *a, SVECTOR *b, SVECTOR *c, SVECTOR *d,
               long *x, long *y, long *z, long *w, long *p, long *otz, long *flag)
{
    clock_psyq_entry();
    port_gte_ldv3(a, b, c); gte_command(GTE_CMD_RTPT);
    uint32_t f = gte_cfc2(GTE_FLAG);
    gte_command(GTE_CMD_NCLIP); *flag = (int32_t)f;
    int32_t result = (int32_t)gte_mfc2(GTE_MAC0);
    if (result > 0) {
        port_gte_stsxy3(x, y, z);
        port_gte_ldv0(d); gte_command(GTE_CMD_RTPS);
        *w = (int32_t)gte_mfc2(GTE_SXY2); *p = (int32_t)gte_mfc2(GTE_IR0);
        *otz = gte_mfc2(GTE_SZ3) >> 2;
        /* DW1 overwrites v0 with the combined flags on the visible path. */
        result = (int32_t)(f | gte_cfc2(GTE_FLAG)); *flag = result;
    }
    return result;
}
