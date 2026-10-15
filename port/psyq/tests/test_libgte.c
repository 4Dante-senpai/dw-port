/* L1: synthetic tables and constant expectations calculated independently in a one-shot
 * Python script from asm:asm/us/main/psyq/libgte.s (including delay slots and 32-bit wrap).
 * The rotation fixtures used a scalar evaluation of that asm, not the C under test.
 * Matrix/projection/color fixtures use separate integer arithmetic; no disc data. */
#include "../psyq.h"
#include "../libgte.h"
#include "../clock.h"
#include "../../backend/gte.h"
#include "../../backend/cdrom.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
#define TIMED(expr) do { uint64_t before = clock_now(); expr; \
    CHECK(clock_now() == before + CLOCK_PSYQ_ENTRY_CYCLES); } while (0)
static MATRIX identity = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, {0, 0, 0}};
static MATRIX rotation = {{{4096, 2048, -1024}, {-2048, 4096, 512}, {1024, -512, 4096}}, {10, -20, 30}};

static uint32_t word(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static void set_half(uint8_t *p, size_t i, int value)
{
    p[i * 2] = (uint8_t)value; p[i * 2 + 1] = (uint8_t)((uint16_t)value >> 8);
}
static void matrices_equal(MATRIX *m, const int16_t expected[9])
{
    for (int i = 0; i < 9; i++) CHECK(m->m[i / 3][i % 3] == expected[i]);
}

static void tables(void)
{
    const char *names[] = {"rsin_tbl", "SQRT", "rcossin_tbl", "ratan_tbl"};
    for (unsigned n = 0; n < 4; n++) {
        size_t size;
        const uint8_t *data = libgte_table(names[n], &size);
        CHECK(data && size > 0);
        for (size_t j = 0; j < size; j++) CHECK(data[j] == 0);
        uint8_t *buf = calloc(1, size);
        CHECK(buf != NULL);
        for (size_t i = 0; i < size / 2; i++) {
            int value;
            if (n == 0) value = (int)i * 3 - 77;
            else if (n == 1) value = 4096 + (int)i * 13;
            else if (n == 2) {
                unsigned index = (unsigned)i / 2;
                value = (int)(index * (i % 2 ? 53 : 37) % 8193) - 4096;
                if (!index) value = i % 2 ? 4096 : 0;
            } else value = (int)i / 2;
            set_half(buf, i, value);
        }
        CHECK(libgte_set_table(names[n], buf, size - 1) == -1);
        CHECK(libgte_set_table(names[n], NULL, size) == -1);
        CHECK(libgte_set_table(names[n], buf, size) == 0);
        CHECK(memcmp(data, buf, size) == 0);
        free(buf);
    }
    size_t size = 123;
    CHECK(libgte_table("unknown", &size) == NULL && size == 0);
    CHECK(libgte_set_table("unknown", &size, sizeof(size)) == -1);
}

static void scalar(void)
{
    static const struct { int a, s, c; } trig[] = {
        {0, -77, 2995}, {1024, 2995, -77}, {2048, -77, -2995}, {3072, -2995, 77},
        {4095, 74, 2992}, {4096, -77, 2995}, {-1, 74, 2992}, {-1024, -2995, -77},
        {-4096, 77, 2995}, {8193, -74, 2992}, {INT_MIN, 77, 2995}
    };
    for (unsigned i = 0; i < sizeof(trig) / sizeof(trig[0]); i++) {
        TIMED(CHECK(rsin(trig[i].a) == trig[i].s));
        TIMED(CHECK(rcos(trig[i].a) == trig[i].c));
    }
    static const struct { long y, x, result; } atan[] = {
        {0,0,0}, {0,1,0}, {1,0,1024}, {0,-1,2048}, {-1,0,-1024},
        {1,2,256}, {2,1,768}, {1,1,512}, {-1,2,-256}, {1,-2,1792}, {-1,-2,-1792},
        {0x200001,0x400001,256}, {0x400001,0x200001,768},
        {0x1fffff,0x400001,255}, {INT_MAX,INT_MAX,512}
    };
    for (unsigned i = 0; i < sizeof(atan) / sizeof(atan[0]); i++)
        TIMED(CHECK(ratan2(atan[i].y, atan[i].x) == atan[i].result));
    static const long roots[][2] = {{0,0},{1,1},{2,1},{4,2},{16,4},{256,16},{65536,256},
                                    {0x40000000,32768},{0x7fffffff,39320}};
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
        TIMED(CHECK(SquareRoot0(roots[i][0]) == roots[i][1]));
        CHECK(gte_mfc2(GTE_LZCS) == (uint32_t)roots[i][0]);
    }
}

static void rotations(void)
{
    static const struct {
        SVECTOR angles;
        int16_t expected[3][9];
    } cases[] = {
        {{0,0,0,0}, {{4096,0,0,0,4096,0,0,0,4096}, {4096,0,0,0,4096,0,0,0,4096}, {4096,0,0,0,4096,0,0,0,4096}}},
        {{17,23,31,0}, {{1722,-2072,-3245,655,3891,-2436,4012,254,2244},
                       {-256,-3716,2531,2300,1913,3467,-3697,877,2244},
                       {1722,-3945,980,2071,-65,-3899,3245,2435,2244}}},
        {{-17,23,-31,0}, {{1722,2071,-3245,-655,3890,2435,4012,-254,2244},
                         {-256,3717,2531,-2301,1913,-3467,-3697,-879,2244},
                         {1722,3946,980,-2072,-65,3899,3245,-2436,2244}}},
        {{17,-23,31,0}, {{1722,-2072,3245,3945,-65,-2436,980,3899,2244},
                        {3699,-426,-2532,2300,1913,3467,190,-3796,2244},
                        {1722,-655,4012,2071,3890,-254,-3245,2435,2244}}},
        {{-32768,-4097,8193,0}, {{3990,-4007,4059,-4059,-4043,0,4006,-4022,-4043},
                               {3990,-4006,4059,-4059,-4043,0,4007,-4023,-4043},
                               {3990,4059,-4007,4006,-4043,-4023,-4059,0,-4043}}},
        {{1,0,0,0}, {{4096,0,0,0,-4043,4059,0,-4059,-4043}, {4096,0,0,0,-4043,4059,0,-4059,-4043}, {4096,0,0,0,-4043,4059,0,-4059,-4043}}},
        {{0,1,0,0}, {{-4043,0,-4059,0,4096,0,4059,0,-4043}, {-4043,0,-4059,0,4096,0,4059,0,-4043}, {-4043,0,-4059,0,4096,0,4059,0,-4043}}},
        {{0,0,1,0}, {{-4043,4059,0,-4059,-4043,0,0,0,4096}, {-4043,4059,0,-4059,-4043,0,0,0,4096}, {-4043,4059,0,-4059,-4043,0,0,0,4096}}}
    };
    MATRIX *(*funcs[])(SVECTOR *, MATRIX *) = {RotMatrix, RotMatrixYXZ, RotMatrixZYX};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (int j = 0; j < 3; j++) {
            MATRIX m;
            memset(&m, 0x5a, sizeof(m));
            SVECTOR a = cases[i].angles;
            TIMED(CHECK(funcs[j](&a, &m) == &m));
            matrices_equal(&m, cases[i].expected[j]);
            CHECK(((uint16_t *)&m)[9] == 0x5a5a);
            CHECK(m.t[0] == 0x5a5a5a5a && m.t[1] == 0x5a5a5a5a && m.t[2] == 0x5a5a5a5a);
        }
    }
}

static void matrix_ops(void)
{
    SVECTOR v = {3, -5, 7, 123}, sv = {0,0,0,456};
    VECTOR out = {0,0,0,789};
    TIMED(CHECK(ApplyMatrix(&rotation, &v, &out) == &out));
    CHECK(out.vx == -2 && out.vy == -6 && out.vz == 8 && out.pad == 789);
    TIMED(CHECK(ApplyMatrixSV(&rotation, &v, &sv) == &sv));
    CHECK(sv.vx == -2 && sv.vy == -6 && sv.vz == 8 && sv.pad == 456);
    VECTOR lv = {70001, -100003, 32769, 321};
    TIMED(CHECK(ApplyMatrixLV(&rotation, &lv, &out) == &out));
    CHECK(out.vx == 11807 && out.vy == -130908 && out.vz == 62769 && out.pad == 789);
    CHECK(gte_mfc2(GTE_VXY0) == 0xfffb0003); /* LV uses IR, not V0. */
    TIMED(CHECK(ApplyTransposeMatrixLV(&rotation, &lv, &out) == &out));
    CHECK(out.vx == 128194 && out.vy == -69099 && out.vz == 2768 && out.pad == 789);
    CHECK(gte_mfc2(GTE_VXY0) == 0xf95d1171 && gte_mfc2(GTE_VZ0) == 1);
    lv = (VECTOR){INT_MAX, INT_MIN, -32769, 321};
    TIMED(ApplyMatrixLV(&identity, &lv, &lv));
    /* The high part is truncated by the signed IR register, just as the asm. */
    CHECK(lv.vx == -1 && lv.vy == 0 && lv.vz == -32769 && lv.pad == 321);
    MATRIX large = {{{32767,32767,32767},{-32768,-32768,-32768},{0,0,4096}}, {0,0,0}};
    v = (SVECTOR){32767,32767,32767,123};
    TIMED(ApplyMatrixSV(&large, &v, &v));
    CHECK(v.vx == 32767 && v.vy == -32768 && v.vz == 32767 && v.pad == 123);
    CHECK(gte_cfc2(GTE_FLAG) == 0x81800000u);

    MATRIX b = {{{2048,-4096,0},{1024,512,4096},{-2048,0,8192}}, {65537,-65538,98307}};
    MATRIX result;
    memset(&result, 0x5a, sizeof(result));
    const int16_t product[9] = {3072,-3840,0,-256,2560,5120,-1664,-1088,7680};
    TIMED(CHECK(MulMatrix0(&rotation, &b, &result) == &result));
    matrices_equal(&result, product);
    CHECK(word((uint8_t *)&result + 16) == 7680 && result.t[0] == 0x5a5a5a5a);
    MATRIX a = rotation;
    TIMED(CHECK(MulMatrix(&a, &b) == &a));
    matrices_equal(&a, product); CHECK(a.t[0] == 10 && a.t[1] == -20 && a.t[2] == 30);
    TIMED(CHECK(CompMatrix(&rotation, &b, &result) == &result));
    matrices_equal(&result, product);
    CHECK(result.t[0] == 8201 && result.t[1] == -4119 && result.t[2] == -32735);
    /* Alias both output variants, including padding sign extension. */
    a = rotation;
    TIMED(MulMatrix0(&a, &b, &b)); matrices_equal(&b, product);
    a = identity; a.m[2][2] = -4096;
    TIMED(MulMatrix0(&a, &identity, &a)); CHECK(word((uint8_t *)&a + 16) == 0xfffff000u);

    VECTOR scale = {8192,-4096,2048,0}; a = rotation;
    const int16_t scaled[9] = {8192,-2048,-512,-4096,-4096,256,2048,512,2048};
    TIMED(CHECK(ScaleMatrix(&a, &scale) == &a)); matrices_equal(&a, scaled);
    CHECK(word((uint8_t *)&a + 16) == 2048);
    scale = (VECTOR){INT_MAX,INT_MIN,-1,0}; a = identity;
    TIMED(ScaleMatrix(&a, &scale));
    CHECK(a.m[0][0] == -1 && a.m[1][1] == 0 && word((uint8_t *)&a + 16) == 0xffffffffu);
    TIMED(CHECK(TransMatrix(&a, &scale) == &a));
    CHECK(a.t[0] == INT_MAX && a.t[1] == INT_MIN && a.t[2] == -1);

    a = (MATRIX){{{1,2,3},{4,5,6},{7,8,9}}, {10,11,12}};
    const int16_t transpose[9] = {1,4,7,2,5,8,3,6,9};
    memset(&result, 0x5a, sizeof(result));
    TIMED(CHECK(TransposeMatrix(&a, &result) == &result)); matrices_equal(&result, transpose);
    CHECK(((uint16_t *)&result)[9] == 0x5a5a && result.t[2] == 0x5a5a5a5a);
    TIMED(TransposeMatrix(&a, &a)); matrices_equal(&a, transpose);
}

static void stack_and_registers(void)
{
    TIMED(SetRotMatrix(&rotation)); TIMED(SetLightMatrix(&rotation)); TIMED(SetTransMatrix(&rotation));
    for (int i = 0; i < 4; i++) {
        CHECK(gte_cfc2(i) == word((uint8_t *)&rotation + i * 4));
        CHECK(gte_cfc2(8 + i) == word((uint8_t *)&rotation + i * 4));
    }
    CHECK(gte_cfc2(4) == 4096 && gte_cfc2(12) == 4096);
    CHECK(gte_cfc2(GTE_TRX) == 10 && gte_cfc2(GTE_TRY) == (uint32_t)-20 && gte_cfc2(GTE_TRZ) == 30);
    uint32_t saved[20][8];
    for (int level = 0; level < 20; level++) {
        for (int i = 0; i < 8; i++) {
            gte_ctc2(i, (uint32_t)(level * 16 + i - 100)); saved[level][i] = gte_cfc2(i);
        }
        TIMED(PushMatrix());
    }
    gte_ctc2(GTE_TRX, 12345); TIMED(PushMatrix()); CHECK(gte_cfc2(GTE_TRX) == 12345);
    for (int level = 19; level >= 0; level--) {
        TIMED(PopMatrix());
        for (int i = 0; i < 8; i++) CHECK(gte_cfc2(i) == saved[level][i]);
        CHECK(gte_cfc2(GTE_LLM0) == word(&rotation));
    }
    TIMED(PopMatrix());
    for (int i = 0; i < 8; i++) CHECK(gte_cfc2(i) == saved[0][i]);
}

static void projection_setup(void)
{
    gte_reset(); SetRotMatrix(&identity);
    gte_ctc2(GTE_H, 500); gte_ctc2(GTE_DQA, 64); gte_ctc2(GTE_DQB, 4096);
}
static void projection(void)
{
    SVECTOR a = {0,0,1000,0}, b = {100,0,1000,0}, c = {0,100,1000,0}, d = {100,100,1000,0};
    long x, y, z, w, p, flag, otz;
    projection_setup();
    TIMED(CHECK(RotTransPers(&b, &x, &p, &flag) == 250));
    CHECK(x == 50 && p == 513 && flag == 0);
    TIMED(CHECK(RotTransPers3(&a, &b, &c, &x, &y, &z, &p, &flag) == 250));
    CHECK(x == 0 && y == 50 && z == 0x00320000 && p == 513 && flag == 0);
    TIMED(CHECK(RotTransPers4(&a, &b, &c, &d, &x, &y, &z, &w, &p, &flag) == 250));
    CHECK(x == 0 && y == 50 && z == 0x00320000 && w == 0x00320032 && p == 513 && flag == 0);
    TIMED(CHECK(RotNclip3(&a, &b, &c, &x, &y, &z, &p, &otz, &flag) == 2500));
    CHECK(x == 0 && y == 50 && z == 0x00320000 && p == 513 && otz == 250 && flag == 0);
    TIMED(CHECK(RotNclip4(&a, &b, &c, &d, &x, &y, &z, &w, &p, &otz, &flag) == 0));
    CHECK(x == 0 && y == 50 && z == 0x00320000 && w == 0x00320032 && p == 513 && otz == 250 && flag == 0);
    x = y = z = w = p = otz = 123456;
    TIMED(CHECK(RotNclip3(&a, &c, &b, &x, &y, &z, &p, &otz, &flag) == -2500));
    CHECK(x == 123456 && y == x && z == x && p == x && otz == x && flag == 0);
    TIMED(CHECK(RotNclip4(&a, &c, &b, &d, &x, &y, &z, &w, &p, &otz, &flag) == -2500));
    CHECK(x == 123456 && y == x && z == x && w == x && p == x && otz == x && flag == 0);
    TIMED(CHECK(RotNclip4(&a, &a, &b, &d, &x, &y, &z, &w, &p, &otz, &flag) == 0));
    CHECK(x == 123456 && w == x && otz == x);
    /* First RTPT overflows screen X; fourth RTPS has no flags. They must be ORed. */
    SVECTOR far = {3000,0,1000,0};
    TIMED(CHECK(RotTransPers4(&a, &far, &c, &d, &x, &y, &z, &w, &p, &flag) == 250));
    CHECK((uint32_t)flag == 0x80004000u && gte_cfc2(GTE_FLAG) == 0);
    TIMED(CHECK((uint32_t)RotNclip4(&a, &far, &c, &d, &x, &y, &z, &w, &p, &otz, &flag) == 0x80004000u));
    CHECK((uint32_t)flag == 0x80004000u && otz == 250);
    d = (SVECTOR){0,0,-1,0};
    TIMED(CHECK((uint32_t)RotNclip4(&a, &b, &c, &d, &x, &y, &z, &w, &p, &otz, &flag) == 0x80060000u));
    CHECK(otz == 0 && p == 2048 && (uint32_t)flag == 0x80060000u);
}

static void colors(void)
{
    gte_reset(); SetLightMatrix(&identity);
    for (int i = 0; i < 5; i++) gte_ctc2(GTE_LCM0 + i, word((uint8_t *)&identity + 4 * i));
    SVECTOR a = {4096,2048,1024,0}, b = {-4096,4096,8192,0}, c = {1024,0,4096,0};
    CVECTOR rgb = {80,120,200,0x34}, x, y, z;
    TIMED(NormalColorCol(&a, &rgb, &x));
    CHECK(word(&x) == 0x34323c50u && gte_cfc2(GTE_FLAG) == 0);
    TIMED(NormalColorCol3(&a, &b, &c, &rgb, &x, &y, &z));
    CHECK(word(&x) == 0x34323c50u && word(&y) == 0x34ff7800u && word(&z) == 0x34c80014u);
    CHECK(gte_mfc2(GTE_RGB0) == word(&x) && gte_mfc2(GTE_RGB1) == word(&y) && gte_mfc2(GTE_RGB2) == word(&z));
    CHECK(gte_cfc2(GTE_FLAG) == 0x81080000u);
}

static void load_errors(void)
{
    char err[256];
    uint8_t buf[16];
    cdrom_close(); CHECK(cdrom_read_file(NULL, 0, buf, sizeof(buf)) == -1);
    CHECK(libgte_load_tables(err, sizeof(err)) == -1);
    CHECK(strstr(err, "missing or short") != NULL);
    CHECK(cdrom_open("pack_t1.pack", NULL, err, sizeof(err)) == 0);
    const PackEntry *exe = cdrom_find_path(PACK_EXE_PATH);
    CHECK(exe != NULL);
    CHECK(cdrom_read_file(exe, 0, buf, sizeof(buf)) == 0);
    CHECK(cdrom_read_file(exe, exe->size - sizeof(buf), buf, sizeof(buf)) == 0);
    CHECK(cdrom_read_file(exe, exe->size - sizeof(buf) + 1, buf, sizeof(buf)) == -1);
    CHECK(cdrom_read_file(exe, UINT32_MAX, buf, sizeof(buf)) == -1);
    CHECK(libgte_load_tables(err, sizeof(err)) == -1);
    const char *names[] = {"rsin_tbl", "SQRT", "rcossin_tbl", "ratan_tbl"};
    for (unsigned i = 0; i < 4; i++) {
        size_t size;
        const uint8_t *data = libgte_table(names[i], &size);
        for (size_t j = 0; j < size; j++) CHECK(data[j] == 0);
    }
    cdrom_close();
}

int main(void)
{
    clock_reset(); gte_reset();
    tables(); scalar(); rotations(); matrix_ops(); stack_and_registers(); projection(); colors(); load_errors();
    printf("libgte: %s\n", failures ? "FAILED" : "OK (29 entries, synthetic tables and Pack errors)");
    return failures ? 1 : 0;
}
