/* Adapted from Wyrelade/DW2-PC backend/mdec.c, CC0 1.0.
 * https://github.com/Wyrelade/DW2-PC
 * Command tables, coefficient order and integer IDCT follow psx-spx:
 * https://psx-spx.consoledev.net/macroblockdecodermdec/
 * Quantization and scale tables are supplied by commands, never embedded.
 */
#include "mdec.h"
#include <string.h>

static const unsigned char zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
};

static uint8_t quant[2][64];
static int16_t scale[64];
static union { uint32_t alignment; uint8_t bytes[768]; } pixel_buffer;
#define pixels pixel_buffer.bytes
static unsigned pixel_pos, pixel_size;
static int m_signed;

static const uint16_t *m_in, *m_end;
static int m_rgb24, m_stp;

void mdec_reset(void)
{
    m_in = m_end = NULL;
    pixel_pos = pixel_size = 0;
}

int mdec_command(uint32_t command, const void *parameters, size_t words)
{
    unsigned kind = command >> 29;
    if (kind == 2) {
        size_t bytes = (command & 1) ? 128 : 64;
        if (words * 4 < bytes) return -1;
        memcpy(quant, parameters, bytes);
    } else if (kind == 3) {
        if (words < 32) return -1;
        memcpy(scale, parameters, sizeof(scale));
    } else if (kind == 1) {
        if (words < (command & 65535) || ((command >> 27) & 3) < 2) return -1;
        m_in = parameters;
        m_end = m_in + (command & 65535) * 2;
        m_rgb24 = ((command >> 27) & 3) == 2;
        m_stp = (command >> 25) & 1;
        m_signed = (command >> 26) & 1;
        pixel_pos = pixel_size = 0;
    }
    return 0;
}

static int signed10(unsigned int v) {
    return (int)(v & 0x3FF) - (int)((v & 0x200) << 1);
}

static int clamp(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

/* Arithmetic division with discarded fractional bits, including negative values. */
static int floor_div(int value, int divisor) {
    return (int)(value / divisor - (value < 0 && value % divisor != 0));
}

static int floor_shift(int64_t value, unsigned bits) {
    return value >= 0 ? (int)(value >> bits) : -1 - (int)((-1 - value) >> bits);
}

/* psx-spx real_idct_core: discard the low three scale bits and round each
 * transposing pass, rounding up only when the fraction is greater than 1/2.
 * The source explicitly describes this hardware model as approximate. */
static void idct(int *blk) {
    int tmp[64];
    int x, y, u;
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (u = 0; u < 8; u++)
                sum += (int64_t)blk[y + u * 8] * floor_shift(scale[x + u * 8], 3);
            tmp[x + y * 8] = floor_shift(sum + 0xfff, 13);
        }
    }
    for (x = 0; x < 8; x++) {
        for (y = 0; y < 8; y++) {
            int64_t sum = 0;
            for (u = 0; u < 8; u++)
                sum += (int64_t)tmp[y + u * 8] * floor_shift(scale[x + u * 8], 3);
            blk[x + y * 8] = floor_shift(sum + 0xfff, 13);
        }
    }
}

/* One block from the stream (0 if the stream has ended). */
static int read_block(int *blk, const uint8_t *qt) {
    unsigned int n;
    int qs, k;

    memset(blk, 0, 64 * sizeof(int));
    do {
        if (!m_in || m_in >= m_end) {
            return 0;
        }
        n = *m_in++;
    } while (n == 0xFE00);
    qs = (int)(n >> 10);
    k = 0;
    if (qs == 0) {
        blk[0] = clamp(signed10(n) * 2, -0x400, 0x3FF);
    } else {
        blk[0] = clamp(signed10(n) * qt[0], -0x400, 0x3FF);
    }
    while (m_in < m_end) {
        n = *m_in++;
        if (n == 0xFE00) {
            break;
        }
        k += (int)(n >> 10) + 1;
        if (k > 63) {
            break;
        }
        if (qs == 0) {
            blk[k] = clamp(signed10(n) * 2, -0x400, 0x3FF);
        } else {
            blk[zigzag[k]] = clamp(floor_shift(signed10(n) * qt[k] * qs + 4, 3), -0x400, 0x3FF);
        }
    }
    idct(blk);
    return 1;
}

/* One 16x16 macroblock (Cr, Cb, Y top left, top right, bottom left, bottom right) into out. */
static void macroblock(uint32_t *out) {
    int cr[64], cb[64], y[4][64];
    int ok = read_block(cr, quant[1]);
    ok &= read_block(cb, quant[1]);
    int i, px, py;

    for (i = 0; i < 4; i++) {
        ok &= read_block(y[i], quant[0]);
    }
    for (py = 0; py < 16; py++) {
        for (px = 0; px < 16; px++) {
            int c = (px >> 1) + (py >> 1) * 8;
            int lum = y[(py >> 3) * 2 + (px >> 3)][(px & 7) + (py & 7) * 8];
            int r = 0, g = 0, b = 0;

            if (ok) {
                /* Decimal coefficients from psx-spx yuv_to_rgb, without an
                 * assumed 10-bit coefficient table. [hypothesis] Nearest,
                 * ties down: the source leaves color precision and rounding
                 * unknown, so this is not a claim of hardware-exact output. */
                r = clamp(lum + floor_div(cr[c] * 14020 + 4999, 10000), -128, 127) + 128;
                g = clamp(lum + floor_div(-cb[c] * 3437 - cr[c] * 7143 + 4999, 10000), -128, 127) + 128;
                b = clamp(lum + floor_div(cb[c] * 17720 + 4999, 10000), -128, 127) + 128;
            }
            if (m_signed) { r ^= 128; g ^= 128; b ^= 128; }
            if (m_rgb24) {
                uint8_t *o = (uint8_t *)out + (py * 16 + px) * 3;
                o[0] = (uint8_t)r;
                o[1] = (uint8_t)g;
                o[2] = (uint8_t)b;
            } else {
                ((uint16_t *)out)[py * 16 + px] =
                    (uint16_t)((r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 | m_stp << 15);
            }
        }
    }
}

void mdec_out(void *out, size_t words)
{
    uint8_t *dst = out;
    size_t bytes = words * 4;
    while (bytes) {
        if (pixel_pos == pixel_size) {
            macroblock((uint32_t *)(void *)pixels);
            pixel_pos = 0;
            pixel_size = m_rgb24 ? 768 : 512;
        }
        size_t count = pixel_size - pixel_pos;
        if (count > bytes) count = bytes;
        memcpy(dst, pixels + pixel_pos, count);
        dst += count;
        bytes -= count;
        pixel_pos += count;
    }
}
