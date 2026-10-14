/* L1: own constant vectors, calculated separately with Python integer arithmetic from
 * https://psx-spx.consoledev.net/geometrytransformationenginegte/ (2026-10-13).
 * No DW2-PC vectors or gte_ref.bin. Expected values never call the model under test.
 * Projection vectors use an identity rotation: M=(V+TR)*4096, then psx-spx UNR,
 * saturation and FLAG rules. Matrix/color vectors use the explicit matrices below. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../psyq/include/inline_n.h"
#include "../../psyq/clock.h"

static int failures;
static const char *scenario;
#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__, scenario, #cond); \
        failures++; \
    } \
} while (0)

static void data(int reg, int32_t value) { CHECK(gte_mfc2(reg) == (uint32_t)value); }
static void flags(uint32_t value) { CHECK(gte_cfc2(GTE_FLAG) == value); }
static uint32_t pair(int x, int y) { return (uint16_t)x | (uint32_t)(uint16_t)y << 16; }

static void vector(int n, int x, int y, int z)
{
    gte_mtc2(GTE_VXY0 + n * 2, pair(x, y));
    gte_mtc2(GTE_VZ0 + n * 2, (uint32_t)z);
}

static void matrix(int base, const int16_t values[9])
{
    for (int i = 0; i < 4; i++)
        gte_ctc2(base + i, pair(values[i * 2], values[i * 2 + 1]));
    gte_ctc2(base + 4, (uint32_t)values[8]);
}

static const int16_t identity[9] = { 4096, 0, 0, 0, 4096, 0, 0, 0, 4096 };

static void reset_state(void)
{
    for (int i = 0; i < 32; i++) {
        data(i, i == GTE_LZCR ? 32 : 0);
        CHECK(gte_cfc2(i) == 0);
    }
}

static void registers_and_macros(void)
{
    scenario = "static initialization";
    reset_state(); /* Must hold even before the first gte_reset. */
    scenario = "unaligned loads and matrix layout";
    unsigned char v[25], m[33];
    const uint32_t words[6] = { 0xFFFE0001, 0x1234FFFD, 0x0005FFFC,
                               0x5678FFFA, 0x00080007, 0x9ABCFFF7 };
    const uint32_t mw[8] = { 0x00020001, 0x00040003, 0x00060005, 0x00080007,
                            0x1234FFF7, 0xFFFFFFF6, 11, 0xFFFFFFF4 };
    memcpy(v + 1, words, sizeof(words));
    memcpy(m + 1, mw, sizeof(mw));
    gte_ldv3(v + 1, v + 9, v + 17);
    for (int i = 0; i < 3; i++) {
        CHECK(gte_mfc2(i * 2) == words[i * 2]);
        data(i * 2 + 1, -3 * (i + 1));
    }
    gte_ldv0(v + 9);
    CHECK(gte_mfc2(GTE_VXY0) == words[2]);
    data(GTE_VZ0, -6);
    gte_SetRotMatrix(m + 1);
    gte_SetTransMatrix(m + 1);
    for (int i = 0; i < 8; i++)
        CHECK(gte_cfc2(i) == (i == 4 ? 0xFFFFFFF7 : mw[i]));

    scenario = "LZCS/LZCR and unaligned 32-bit store";
    static const struct { uint32_t input, expected; } lzc[] = {
        { 0, 32 }, { 1, 31 }, { 0xFFFFFFFF, 32 }, { 0x80000000, 1 }, { 0x00FFFFFF, 8 }
    };
    for (unsigned i = 0; i < sizeof(lzc) / sizeof(lzc[0]); i++) {
        unsigned char out[6];
        uint32_t value;
        memset(out, 0xA5, sizeof(out));
        gte_ldlzc((long)(int32_t)lzc[i].input);
        gte_stlzc(out + 1);
        memcpy(&value, out + 1, 4);
        CHECK(value == lzc[i].expected);
        CHECK(out[0] == 0xA5 && out[5] == 0xA5);
        CHECK(gte_mfc2(GTE_LZCS) == lzc[i].input);
        gte_mtc2(GTE_LZCR, 999);
        CHECK(gte_mfc2(GTE_LZCR) == lzc[i].expected);
    }

    scenario = "register write rules";
    for (int i = GTE_IR0; i <= GTE_IR3; i++) {
        gte_mtc2(i, 0x12348000);
        data(i, -32768);
    }
    static const int unsigned_regs[] = { GTE_OTZ, GTE_SZ0, GTE_SZ1, GTE_SZ2, GTE_SZ3 };
    for (unsigned i = 0; i < sizeof(unsigned_regs) / sizeof(unsigned_regs[0]); i++) {
        gte_mtc2(unsigned_regs[i], 0xABCDFFFF);
        data(unsigned_regs[i], 65535);
    }
    static const int signed_ctrl[] = { 4, 12, 20, GTE_H, GTE_DQA, GTE_ZSF3, GTE_ZSF4 };
    for (unsigned i = 0; i < sizeof(signed_ctrl) / sizeof(signed_ctrl[0]); i++) {
        gte_ctc2(signed_ctrl[i], 0x12348000);
        CHECK(gte_cfc2(signed_ctrl[i]) == 0xFFFF8000);
    }
    gte_ctc2(GTE_FLAG, 0xFFFFFFFF);
    flags(0xFFFFF000);
    gte_ctc2(GTE_FLAG, 0x80781000); /* IR3, color and IR0 do not set summary. */
    flags(0x00781000);
    gte_ctc2(GTE_FLAG, 0x80000FFF);
    flags(0);
    gte_mtc2(GTE_IRGB, 0xFFFF7C1F);
    data(GTE_IR1, 3968); data(GTE_IR2, 0); data(GTE_IR3, 3968);
    data(GTE_IRGB, 0x7C1F); data(GTE_ORGB, 0x7C1F);
    gte_mtc2(GTE_ORGB, 0);
    data(GTE_ORGB, 0x7C1F);
    gte_mtc2(GTE_IR1, (uint32_t)-1);
    gte_mtc2(GTE_IR2, 0x7FFF);
    gte_mtc2(GTE_IR3, 128);
    data(GTE_ORGB, 0x7E0);
    gte_mtc2(GTE_SXY0, 1); gte_mtc2(GTE_SXY1, 2); gte_mtc2(GTE_SXY2, 3);
    gte_mtc2(GTE_SXYP, 4);
    data(GTE_SXY0, 2); data(GTE_SXY1, 3); data(GTE_SXY2, 4); data(GTE_SXYP, 4);
    scenario = "reset after writes";
    gte_reset();
    reset_state();
}

static void rtps_vectors(void)
{
    static const struct {
        const char *name;
        int v[3], t[3], h, offset[2], dqa, dqb, sf, lm;
        int64_t expected[7]; /* SXY2, SZ3, IR1..3, MAC0, IR0 */
        uint32_t flag;
        int32_t mac[3];
    } cases[] = {
        { "normal", { 100, -50, 1000 }, { 10, -20, 0 }, 500, { 10485760, 7864320 }, 64, 4096, 1, 0,
          { 5570775, 1000, 110, -70, 1000, 2101248, 513 }, 0x00000000u, { 110, -70, 1000 } }, /* UNR quotient 32768 */
        { "ir_xy", { 100, -50, 1000 }, { 40000, -40000, 0 }, 0, { 0, 0 }, 0, 0, 1, 0,
          { 0, 1000, 32767, -32768, 1000, 0, 0 }, 0x81800000u, { 40100, -40050, 1000 } }, /* UNR quotient 0 */
        { "z_negative", { 0, 0, -1 }, { 0, 0, 0 }, 1, { 0, 0 }, 0, 0, 1, 0,
          { 0, 0, 0, 0, -1, 0, 0 }, 0x80060000u, { 0, 0, -1 } }, /* UNR quotient 131071 */
        { "z_large", { 0, 0, 1000 }, { 0, 0, 70000 }, 0, { 0, 0 }, 0, 0, 1, 0,
          { 0, 65535, 0, 0, 32767, 0, 0 }, 0x80440000u, { 0, 0, 71000 } }, /* UNR quotient 0 */
        { "near_equal", { 0, 0, 100 }, { 0, 0, 0 }, 200, { 0, 0 }, 0, 0, 1, 0,
          { 0, 100, 0, 0, 100, 0, 0 }, 0x80020000u, { 0, 0, 100 } }, /* UNR quotient 131071 */
        { "screen", { 2000, -2000, 1000 }, { 0, 0, 0 }, 1000, { 0, 0 }, 0, 0, 1, 0,
          { 4227859455, 1000, 2000, -2000, 1000, 0, 0 }, 0x80006000u, { 2000, -2000, 1000 } }, /* UNR quotient 65536 */
        { "cue_negative", { 0, 0, 1000 }, { 0, 0, 0 }, 500, { 0, 0 }, -1, 0, 1, 0,
          { 0, 1000, 0, 0, 1000, -32768, 0 }, 0x00001000u, { 0, 0, 1000 } }, /* UNR quotient 32768 */
        { "cue_large", { 0, 0, 1000 }, { 0, 0, 0 }, 500, { 0, 0 }, 1024, 0, 1, 0,
          { 0, 1000, 0, 0, 1000, 33554432, 4096 }, 0x00001000u, { 0, 0, 1000 } }, /* UNR quotient 32768 */
        { "unr", { 123, -234, 457 }, { 0, 0, 0 }, 123, { 10485760, 7864320 }, 1, 0, 1, 0,
          { 3735745, 457, 123, -234, 457, 17639, 4 }, 0x00000000u, { 123, -234, 457 } }, /* UNR quotient 17639 */
        { "unr_end", { 100, -100, 0 }, { 0, 0, 65535 }, 65535, { 0, 0 }, 1, 0, 1, 0,
          { 4288413795, 65535, 100, -100, 32767, 65535, 15 }, 0x00400000u, { 100, -100, 65535 } }, /* UNR quotient 65535 */
        { "unr_high", { 0, 0, 0 }, { 0, 0, 65535 }, 65087, { 0, 0 }, 1, 0, 1, 0,
          { 0, 65535, 0, 0, 32767, 65087, 15 }, 0x00400000u, { 0, 0, 65535 } }, /* UNR quotient 65087 */
        { "unr_rounding_clamp", { 0, 0, 21384 }, { 0, 0, 0 }, 42767, { 0, 0 }, 1, 0, 1, 0,
          { 0, 21384, 0, 0, 21384, 131071, 31 }, 0, { 0, 0, 21384 } },
        { "sf_zero", { 2, -3, 10 }, { 0, 0, 0 }, 0, { 0, 0 }, 0, 0, 0, 0,
          { 0, 10, 8192, -12288, 32767, 0, 0 }, 0x00000000u, { 8192, -12288, 40960 } }, /* UNR quotient 0 */
        { "lm", { 0, 0, -1 }, { 0, 0, 0 }, 0, { 0, 0 }, 0, 0, 1, 1,
          { 0, 0, 0, 0, 0, 0, 0 }, 0x80060000u, { 0, 0, -1 } }, /* UNR quotient 131071 */
        { "mac0_pos", { 32767, 32767, 1000 }, { 0, 0, 0 }, 1999, { 0, 0 }, 32767, 2147483647, 1, 0,
          { 67044351, 1000, 32767, 32767, 1000, 2145222720, 4096 }, 0x80017000u, { 32767, 32767, 1000 } }, /* UNR quotient 131007 */
        { "mac0_neg", { -32768, -32768, 1000 }, { 0, 0, 0 }, 1999, { 0, 0 }, -32768, -2147483648, 1, 0,
          { 4227922944, 1000, -32768, -32768, 1000, -2145353728, 0 }, 0x8000F000u, { -32768, -32768, 1000 } }, /* UNR quotient 131007 */
        { "ir3_only", { 0, 0, 32767 }, { 0, 0, 1 }, 0, { 0, 0 }, 0, 0, 1, 0,
          { 0, 32768, 0, 0, 32767, 0, 0 }, 0x00400000u, { 0, 0, 32768 } }, /* UNR quotient 0 */
        { "screen_bounds", { -1024, 1023, 1000 }, { 0, 0, 0 }, 1000, { 0, 0 }, 0, 0, 1, 0,
          { 67107840, 1000, -1024, 1023, 1000, 0, 0 }, 0x00000000u, { -1024, 1023, 1000 } }, /* UNR quotient 65536 */
        { "cue_bound", { 0, 0, 1000 }, { 0, 0, 0 }, 0, { 0, 0 }, 0, 16777216, 1, 0,
          { 0, 1000, 0, 0, 1000, 16777216, 4096 }, 0x00000000u, { 0, 0, 1000 } }, /* UNR quotient 0 */
    };
    const int regs[] = { GTE_SXY2, GTE_SZ3, GTE_IR1, GTE_IR2, GTE_IR3, GTE_MAC0, GTE_IR0 };
    for (unsigned n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
        scenario = cases[n].name;
        gte_reset();
        matrix(GTE_RT0, identity);
        vector(0, cases[n].v[0], cases[n].v[1], cases[n].v[2]);
        for (int i = 0; i < 3; i++) gte_ctc2(GTE_TRX + i, (uint32_t)cases[n].t[i]);
        gte_ctc2(GTE_H, (uint32_t)cases[n].h);
        gte_ctc2(GTE_OFX, (uint32_t)cases[n].offset[0]);
        gte_ctc2(GTE_OFY, (uint32_t)cases[n].offset[1]);
        gte_ctc2(GTE_DQA, (uint32_t)cases[n].dqa);
        gte_ctc2(GTE_DQB, (uint32_t)cases[n].dqb);
        gte_ctc2(GTE_FLAG, 0x7FFFF000); /* Every command must clear the old flags. */
        if (cases[n].sf == 1 && cases[n].lm == 0)
            gte_rtps();
        else
            gte_command(0x4A000001u | (uint32_t)cases[n].sf << 19 | (uint32_t)cases[n].lm << 10);
        for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
            CHECK(gte_mfc2(regs[i]) == (uint32_t)cases[n].expected[i]);
        flags(cases[n].flag);
        for (int i = 0; i < 3; i++) data(GTE_MAC1 + i, cases[n].mac[i]);
        unsigned char out[6];
        uint32_t word;
        memset(out, 0xA5, sizeof(out));
        gte_stsxy(out + 1);
        memcpy(&word, out + 1, 4);
        CHECK(word == (uint32_t)cases[n].expected[0]);
        CHECK(out[0] == 0xA5 && out[5] == 0xA5);
    }

    scenario = "RTPS non-diagonal rotation";
    const int16_t rotation[9] = { 4096, 2048, -1024, -2048, 4096, 512, 1024, -512, 4096 };
    gte_reset(); matrix(GTE_RT0, rotation); vector(0, 3, -5, 7);
    gte_ctc2(GTE_TRZ, 100); gte_ctc2(GTE_H, 50); gte_ctc2(GTE_DQA, 1);
    gte_rtps();
    data(GTE_MAC1, -2); data(GTE_MAC2, -6); data(GTE_MAC3, 108);
    data(GTE_IR1, -2); data(GTE_IR2, -6); data(GTE_IR3, 108);
    data(GTE_SZ3, 108); data(GTE_SXY2, (int32_t)0xFFFDFFFF);
    data(GTE_MAC0, 30341); data(GTE_IR0, 7); flags(0);
}

static void rtpt_fifo(void)
{
    scenario = "RTPT FIFO and macro stores";
    gte_reset();
    matrix(GTE_RT0, identity);
    gte_ctc2(GTE_H, 500);
    gte_ctc2(GTE_OFX, 10485760); gte_ctc2(GTE_OFY, 7864320);
    gte_ctc2(GTE_DQA, 64); gte_ctc2(GTE_DQB, 4096);
    vector(0, 100, -50, 1000); vector(1, -80, 40, 2000); vector(2, 30, 60, 500);
    for (int i = 0; i < 4; i++) gte_mtc2(GTE_SZ0 + i, (uint32_t)(11 + i));
    gte_rtpt();
    data(GTE_SXY0, 6226130); data(GTE_SXY1, 8519820); data(GTE_SXY2, 11796670);
    data(GTE_SZ0, 14); data(GTE_SZ1, 1000); data(GTE_SZ2, 2000); data(GTE_SZ3, 500);
    data(GTE_IR1, 30); data(GTE_IR2, 60); data(GTE_IR3, 500);
    data(GTE_MAC1, 30); data(GTE_MAC2, 60); data(GTE_MAC3, 500);
    data(GTE_IR0, 1025); data(GTE_MAC0, 4198400); flags(0);
    unsigned char out[21];
    uint32_t word;
    memset(out, 0xA5, sizeof(out));
    gte_stsxy3(out + 1, out + 6, out + 11);
    gte_mtc2(GTE_OTZ, 9999); /* stszotz must use SZ3 >> 2, not OTZ or a 16-bit store. */
    gte_stszotz(out + 16);
    const uint32_t stored[] = { 6226130, 8519820, 11796670, 125 };
    for (int i = 0; i < 4; i++) {
        memcpy(&word, out + 1 + i * 5, 4);
        CHECK(word == stored[i]);
        CHECK(out[i * 5] == 0xA5);
    }
    CHECK(out[20] == 0xA5);
    gte_mtc2(GTE_SZ3, 65535);
    gte_stszotz(out + 1);
    memcpy(&word, out + 1, 4);
    CHECK(word == 16383);

    scenario = "RTPT depth cue only on V2";
    gte_reset(); matrix(GTE_RT0, identity);
    vector(0, 0, 0, 51); vector(1, 0, 0, 100); vector(2, 0, 0, 1000);
    gte_ctc2(GTE_H, 100); gte_ctc2(GTE_DQA, 32767);
    gte_ctc2(GTE_DQB, (uint32_t)-210000000);
    gte_rtpt();
    data(GTE_MAC0, 4754918); data(GTE_IR0, 1160); flags(0);

    scenario = "RTPT accumulates flags across vertices";
    gte_ctc2(GTE_DQA, 0); gte_ctc2(GTE_DQB, 0);
    vector(0, 2000, 0, 100); vector(1, 0, -2000, 100);
    gte_rtpt();
    data(GTE_SXY0, 1023); data(GTE_SXY1, (int32_t)0xFC000000);
    data(GTE_SXY2, 0); data(GTE_SZ3, 1000); flags(0x80006000);
    gte_reset(); reset_state();
}

static void nclip_avsz(void)
{
    scenario = "NCLIP orientations and collinear points";
    gte_reset();
    gte_mtc2(GTE_SXY0, pair(0, 0)); gte_mtc2(GTE_SXY1, pair(10, 0));
    gte_mtc2(GTE_SXY2, pair(0, 10));
    gte_command(GTE_CMD_NCLIP); data(GTE_MAC0, 100); flags(0);
    gte_mtc2(GTE_SXY1, pair(0, 10)); gte_mtc2(GTE_SXY2, pair(10, 0));
    gte_command(GTE_CMD_NCLIP); data(GTE_MAC0, -100); flags(0);
    gte_mtc2(GTE_SXY1, pair(5, 0));
    gte_command(GTE_CMD_NCLIP); data(GTE_MAC0, 0); flags(0);
    scenario = "NCLIP MAC0 overflow and wrap";
    gte_mtc2(GTE_SXY0, pair(-32768, -32768));
    gte_mtc2(GTE_SXY1, pair(32767, -32768));
    gte_mtc2(GTE_SXY2, pair(-32768, 32767));
    gte_command(GTE_CMD_NCLIP); data(GTE_MAC0, -131071); flags(0x80010000);
    gte_mtc2(GTE_SXY1, pair(-32768, 32767));
    gte_mtc2(GTE_SXY2, pair(32767, -32768));
    gte_command(GTE_CMD_NCLIP); data(GTE_MAC0, 131071); flags(0x80008000);

    scenario = "AVSZ3/4 normal and saturation";
    static const struct { uint32_t op; int scale, mac, otz; uint32_t flag; } cases[] = {
        { GTE_CMD_AVSZ3, 4096, 805294080, 65535, 0x80040000 },
        { GTE_CMD_AVSZ4, 4096, 1073725440, 65535, 0x80040000 },
        { GTE_CMD_AVSZ3, 32767, 2147188739, 65535, 0x80050000 },
        { GTE_CMD_AVSZ4, 32767, -393212, 65535, 0x80050000 },
        { GTE_CMD_AVSZ3, -32768, -2147385344, 0, 0x80048000 },
        { GTE_CMD_AVSZ4, -32768, 131072, 0, 0x80048000 },
        { GTE_CMD_AVSZ3, 0, 0, 0, 0 },
    };
    gte_reset();
    for (int i = 0; i < 4; i++) gte_mtc2(GTE_SZ0 + i, (uint32_t)(100 * (i + 1)));
    gte_ctc2(GTE_ZSF3, 1024); gte_ctc2(GTE_ZSF4, 512);
    gte_command(GTE_CMD_AVSZ3); data(GTE_MAC0, 921600); data(GTE_OTZ, 225); flags(0);
    gte_command(GTE_CMD_AVSZ4); data(GTE_MAC0, 512000); data(GTE_OTZ, 125); flags(0);
    for (int i = 0; i < 4; i++) gte_mtc2(GTE_SZ0 + i, 65535);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        gte_ctc2(GTE_ZSF3, (uint32_t)cases[i].scale);
        gte_ctc2(GTE_ZSF4, (uint32_t)cases[i].scale);
        gte_command(cases[i].op);
        data(GTE_MAC0, cases[i].mac); data(GTE_OTZ, cases[i].otz); flags(cases[i].flag);
        for (int j = 0; j < 4; j++) data(GTE_SZ0 + j, 65535);
    }
}

static void mvmva_vectors(void)
{
    scenario = "MVMVA forms used by libgte.s and lm";
    const int16_t m[9] = { 4096, 2048, -1024, -2048, 4096, 512, 1024, -512, 4096 };
    const int32_t mac[2][3] = { { -5120, -23040, 34304 }, { -2, -6, 8 } };
    for (int sf = 0; sf <= 1; sf++) {
        for (int v = 0; v <= 3; v++) {
            for (int lm = 0; lm <= 1; lm++) {
                gte_reset(); matrix(GTE_RT0, m);
                vector(v == 3 ? 0 : v, 3, -5, 7);
                if (v == 3) vector(0, 1000, 1000, 1000); /* IR form must not read V0. */
                gte_mtc2(GTE_IR1, 3); gte_mtc2(GTE_IR2, (uint32_t)-5); gte_mtc2(GTE_IR3, 7);
                gte_ctc2(GTE_TRX, 9999); /* cv=3 must ignore translation. */
                gte_command(GTE_CMD_MVMVA(sf, 0, v, 3, lm));
                for (int i = 0; i < 3; i++) data(GTE_MAC1 + i, mac[sf][i]);
                data(GTE_IR1, lm ? 0 : mac[sf][0]);
                data(GTE_IR2, lm ? 0 : mac[sf][1]);
                data(GTE_IR3, sf ? 8 : 32767);
                flags((lm ? 0x81800000u : 0) | (sf ? 0 : 0x00400000));
            }
        }
    }
    scenario = "MVMVA light/color selection and translation";
    for (int mx = 0; mx < 3; mx++) {
        for (int cv = 0; cv < 2; cv++) {
            gte_reset(); matrix(mx * 8, identity); vector(2, 10, -20, 30);
            gte_ctc2(cv ? GTE_RBK : GTE_TRX, (uint32_t)-100);
            gte_ctc2(cv ? GTE_GBK : GTE_TRY, 200);
            gte_ctc2(cv ? GTE_BBK : GTE_TRZ, (uint32_t)-300);
            gte_command(GTE_CMD_MVMVA(1, mx, 2, cv, 0));
            data(GTE_MAC1, -90); data(GTE_MAC2, 180); data(GTE_MAC3, -270);
            data(GTE_IR1, -90); data(GTE_IR2, 180); data(GTE_IR3, -270); flags(0);
        }
    }
    scenario = "44-bit accumulation wraps and records each channel";
    const int16_t huge[9] = { 32767, 0, 0, 32767, 0, 0, 32767, 0, 0 };
    for (int negative = 0; negative < 2; negative++) {
        gte_reset(); matrix(GTE_RT0, huge);
        vector(0, negative ? -32768 : 32767, 0, 0);
        for (int i = 0; i < 3; i++) gte_ctc2(GTE_TRX + i, negative ? 0x80000000 : 0x7FFFFFFF);
        gte_command(GTE_CMD_MVMVA(1, 0, 0, 0, 0));
        for (int i = 0; i < 3; i++) {
            data(GTE_MAC1 + i, negative ? 2147221512 : -2147221521);
            data(GTE_IR1 + i, negative ? 32767 : -32768);
        }
        flags(negative ? 0x8FC00000 : 0xF1C00000);
    }
    scenario = "44-bit intermediate overflow survives cancellation";
    const int16_t cancel[9] = { 32767, -32767, 0, 0, 0, 0, 0, 0, 0 };
    gte_reset(); matrix(GTE_RT0, cancel); vector(0, 32767, 32767, 0);
    gte_ctc2(GTE_TRX, 0x7FFFFFFF);
    gte_command(GTE_CMD_MVMVA(1, 0, 0, 0, 0));
    data(GTE_MAC1, 2147483647); data(GTE_IR1, 32767); flags(0xC9000000);
}

static void color_vectors(void)
{
    scenario = "NCCS/NCCT light, background, color and FIFO";
    const int16_t light[9] = { 4096, 2048, 0, 0, 4096, 1024, 1024, 0, 4096 };
    const int16_t color[9] = { 4096, 0, 2048, 1024, 4096, 0, 0, 2048, 4096 };
    const int32_t rgb[] = { 0x34293878, 0x34222EC4, 0x341E5266 };
    const int32_t mac[3][3] = { { 1920, 896, 656 }, { 3136, 736, 544 }, { 1632, 1312, 480 } };
    gte_reset(); matrix(GTE_LLM0, light); matrix(GTE_LCM0, color);
    gte_ctc2(GTE_RBK, 128); gte_ctc2(GTE_GBK, 256); gte_ctc2(GTE_BBK, 512);
    gte_mtc2(GTE_RGBC, 0x34204080);
    vector(0, 1024, 2048, 3072); vector(1, 4096, 1024, 2048); vector(2, 512, 4096, 1024);
    gte_mtc2(GTE_RGB0, 1); gte_mtc2(GTE_RGB1, 2); gte_mtc2(GTE_RGB2, 3);
    gte_command(GTE_CMD_NCCS);
    data(GTE_RGB0, 2); data(GTE_RGB1, 3); data(GTE_RGB2, rgb[0]); flags(0);
    for (int i = 0; i < 3; i++) { data(GTE_MAC1 + i, mac[0][i]); data(GTE_IR1 + i, mac[0][i]); }
    gte_command(GTE_CMD_NCCT); flags(0);
    for (int i = 0; i < 3; i++) {
        data(GTE_RGB0 + i, rgb[i]); data(GTE_MAC1 + i, mac[2][i]); data(GTE_IR1 + i, mac[2][i]);
    }

    scenario = "NCCS color saturation alone does not set FLAG bit31";
    gte_reset(); matrix(GTE_LLM0, identity); matrix(GTE_LCM0, identity);
    gte_mtc2(GTE_RGBC, 0xABFFFFFF);
    vector(0, 8192, 8192, 8192);
    gte_command(GTE_CMD_NCCS);
    data(GTE_RGB2, (int32_t)0xABFFFFFF); flags(0x00380000);
    for (int i = 0; i < 3; i++) { data(GTE_MAC1 + i, 8160); data(GTE_IR1 + i, 8160); }
    scenario = "NCCT color flags accumulate before a clean last vertex";
    vector(0, 8192, 0, 0); vector(1, 0, 8192, 8192); vector(2, 0, 0, 0);
    gte_command(GTE_CMD_NCCT);
    data(GTE_RGB0, (int32_t)0xAB0000FF); data(GTE_RGB1, (int32_t)0xABFFFF00);
    data(GTE_RGB2, (int32_t)0xAB000000); flags(0x00380000);
    scenario = "negative light obeys lm";
    vector(0, -4096, -4096, -4096);
    gte_command(GTE_CMD_NCCS); data(GTE_RGB2, (int32_t)0xAB000000); flags(0x81C00000);
    gte_command(GTE_CMD_NCCS & ~0x400u);
    data(GTE_RGB2, (int32_t)0xAB000000); flags(0x00380000);
    for (int i = 0; i < 3; i++) { data(GTE_MAC1 + i, -4080); data(GTE_IR1 + i, -4080); }
    scenario = "light/color IR overflow and sf zero";
    const int16_t twice[9] = { 8192, 0, 0, 0, 8192, 0, 0, 0, 8192 };
    matrix(GTE_LLM0, twice); matrix(GTE_LCM0, twice);
    vector(0, 32767, 32767, 32767);
    gte_command(GTE_CMD_NCCS);
    data(GTE_RGB2, (int32_t)0xABFFFFFF); flags(0x81F80000);
    for (int i = 0; i < 3; i++) { data(GTE_MAC1 + i, 32639); data(GTE_IR1 + i, 32639); }
    matrix(GTE_LLM0, identity); matrix(GTE_LCM0, identity); vector(0, 1, 1, 1);
    gte_command(GTE_CMD_NCCS & ~0x80000u);
    for (int i = 0; i < 3; i++) { data(GTE_MAC1 + i, 133689360); data(GTE_IR1 + i, 32767); }
    flags(0x81F80000);
}

static void command_aborts(uint32_t op)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        if (setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }) != 0) _exit(2);
        if (!freopen("/dev/null", "w", stderr)) _exit(2);
        gte_command(op);
        _exit(0);
    }
    int status;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
}

int main(void)
{
    uint64_t before = clock_now();
    registers_and_macros();
    rtps_vectors();
    rtpt_fifo();
    nclip_avsz();
    mvmva_vectors();
    color_vectors();
    scenario = "unsupported commands and MVMVA selectors abort";
    command_aborts(0x4A000000); command_aborts(0x4AC8041E); /* NCS */
    command_aborts(0x4B90003D); command_aborts(0x4AE80413); /* GPF, NCDS */
    command_aborts(GTE_CMD_MVMVA(1, 3, 0, 3, 0));
    command_aborts(GTE_CMD_MVMVA(1, 0, 0, 2, 0));
    scenario = "inline GTE operations do not advance virtual time";
    CHECK(clock_now() == before);
    printf("test_gte: %s\n", failures ? "FAIL" : "OK");
    return failures != 0;
}
