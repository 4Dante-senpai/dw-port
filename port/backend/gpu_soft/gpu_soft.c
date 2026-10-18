/* Adapted from DW2-PC (CC0): backend/psxgpu.c @ 816ed0d */
#include "../gpu.h"
#include <stdio.h>
#include <string.h>

/* Software-only GP0 model. Pixel rules: psx-spx graphicsprocessingunitgpu.
 * State and VRAM remain private; no platform or presentation dependencies. */
typedef struct {
    int clip_x0, clip_y0, clip_x1, clip_y1, ofs_x, ofs_y;
    uint32_t tpage;
    int dither, draw_disp, flip_x, flip_y;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y, set_mask, check_mask;
} GpuState;
typedef struct { int x, y, r, g, b, u, v; } GpuVertex;
typedef struct { int mode, tx, ty, cx, cy, raw; } GpuTexture;
static uint64_t pixels_written;

static uint16_t vram[GPU_VRAM_H][GPU_VRAM_W];

static GpuState st;

static struct {
    int x, y, w, h, rgb24, on;
} disp;

typedef GpuVertex Vtx;
typedef GpuTexture Tex;

static void log_once(int op, const char *what) {
    static unsigned char seen[256];

    if (!seen[op & 0xFF]) {
        seen[op & 0xFF] = 1;
        printf("[gpu] GP0 %02X: %s (logged once)\n", op & 0xFF, what);
    }
}

static int sign11(uint32_t v) {
    return (int)(v << 21) >> 21;
}

void gpu_reset(int display) {
    pixels_written = 0;
    memset(&st, 0, sizeof(st));
    st.clip_x1 = GPU_VRAM_W - 1;
    st.clip_y1 = GPU_VRAM_H - 1;
    if (display) {
        disp.on = 0;
    }
}

/* ---- pixels ---- */

static void put(int x, int y, int r, int g, int b, int semi, int abr, int mask) {
    uint16_t *d = &vram[y][x];

    if (st.check_mask && (*d & 0x8000)) {
        return;
    }
    if (semi) {
        int br = *d & 31;
        int bg = (*d >> 5) & 31;
        int bb = (*d >> 10) & 31;

        switch (abr) {
        case 0:
            r = (br + r) >> 1;
            g = (bg + g) >> 1;
            b = (bb + b) >> 1;
            break;
        case 1:
            r = br + r;
            g = bg + g;
            b = bb + b;
            break;
        case 2:
            r = br - r;
            g = bg - g;
            b = bb - b;
            break;
        default:
            r = br + (r >> 2);
            g = bg + (g >> 2);
            b = bb + (b >> 2);
            break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        g = g < 0 ? 0 : g > 31 ? 31 : g;
        b = b < 0 ? 0 : b > 31 ? 31 : b;
    }
    pixels_written++;
    *d = (uint16_t)(r | (g << 5) | (b << 10) | ((mask || st.set_mask) ? 0x8000 : 0));
}

static uint16_t texel(const Tex *t, int u, int v) {
    int y;

    u = ((u & ~st.tw_mask_x) | (st.tw_off_x & st.tw_mask_x)) & 0xFF;
    v = ((v & ~st.tw_mask_y) | (st.tw_off_y & st.tw_mask_y)) & 0xFF;
    y = (t->ty + v) & (GPU_VRAM_H - 1);
    switch (t->mode) {
    case 0: {
        uint16_t w = vram[y][(t->tx + (u >> 2)) & (GPU_VRAM_W - 1)];
        return vram[t->cy][(t->cx + ((w >> ((u & 3) * 4)) & 0xF)) & (GPU_VRAM_W - 1)];
    }
    case 1: {
        uint16_t w = vram[y][(t->tx + (u >> 1)) & (GPU_VRAM_W - 1)];
        return vram[t->cy][(t->cx + ((w >> ((u & 1) * 8)) & 0xFF)) & (GPU_VRAM_W - 1)];
    }
    default:
        return vram[y][(t->tx + u) & (GPU_VRAM_W - 1)];
    }
}

/* PS1 dither offsets by (y & 3, x & 3), added to the 8-bit colour before the cut to 5 bits. */
static const int dither_tbl[4][4] = {
    { -4, 0, -3, 1 },
    { 2, -2, 3, -1 },
    { -3, 1, -4, 0 },
    { 3, -1, 2, -2 },
};

static int to5(int c8, int d) {
    c8 += d;
    c8 = c8 < 0 ? 0 : c8 > 255 ? 255 : c8;
    return c8 >> 3;
}

/* One pixel of a primitive: colour r, g, b in 8 bits per channel (vertex colour). dith: the
 * primitive is dithered (E1 bit 9 on a Gouraud or texture-modulated polygon; never rectangles
 * or raw textures). */
static void shade(int x, int y, const Tex *t, int u, int v, int r, int g, int b, int semi, int abr, int dith) {
    int d = dith ? dither_tbl[y & 3][x & 3] : 0;

    if (t != NULL) {
        uint16_t c = texel(t, u, v);
        int tr, tg, tb;

        if (c == 0) {
            return;
        }
        tr = c & 31;
        tg = (c >> 5) & 31;
        tb = (c >> 10) & 31;
        if (!t->raw) {
            /* texel (5 bit) * colour (8 bit) / 16 = 8-bit result, 0x80 = unchanged */
            tr = to5((tr * r) >> 4, d);
            tg = to5((tg * g) >> 4, d);
            tb = to5((tb * b) >> 4, d);
        }
        put(x, y, tr, tg, tb, semi && (c & 0x8000), abr, c & 0x8000);
    } else {
        put(x, y, to5(r, d), to5(g, d), to5(b, d), semi, abr, 0);
    }
}

/* ---- primitives ---- */

static long long edge(const Vtx *a, const Vtx *b, int px, int py) {
    return (long long)(b->x - a->x) * (py - a->y) - (long long)(b->y - a->y) * (px - a->x);
}

/* Top or left edge (included on ties) for a triangle with positive area under edge(). */
static int top_left(const Vtx *a, const Vtx *b) {
    int dx = b->x - a->x;
    int dy = b->y - a->y;

    return dy < 0 || (dy == 0 && dx > 0);
}

/* ref: story 3.2, black-box title PPM vs TITLE256.TIM (pinned in
 * title.toml). A 256-wide UV 0..255 quad samples x up to column 128
 * in its upper triangle; the lower rounds ties down except at exact
 * diagonal intersections (Y multiples of 15). Truncated
 * 16.16 edge intersections, followed by nearest UV sampling, explain the seam:
 * the descending diagonal has slopes -256/240 in X and -255/240 in U.
 * No reference renderer source was consulted. General parity is deferred. */
typedef struct { long long left, u, v; } GpuTextureRow;

static long long uv_gradient(const Vtx *a, const Vtx *b, const Vtx *c,
                             long long area, int use_v) {
    int av = use_v ? a->v : a->u;
    int bv = use_v ? b->v : b->u;
    int cv = use_v ? c->v : c->u;
    return ((long long)(bv-av)*(c->y-a->y)
          - (long long)(cv-av)*(b->y-a->y))*65536 / area;
}

static GpuTextureRow texture_row(const Vtx *a, const Vtx *b, const Vtx *c, int y) {
    const Vtx *vertices[] = {a, b, c};
    GpuTextureRow row = {0, 0, 0};
    int found = 0;
    for (int i = 0; i < 3; i++) {
        const Vtx *lo = vertices[i], *hi = vertices[(i+1)%3];
        if (lo->y > hi->y) { const Vtx *swap=lo; lo=hi; hi=swap; }
        if (y < lo->y || y >= hi->y) continue;
        long long edge_x = (long long)lo->x*65536
            + (long long)(hi->x-lo->x)*65536*(y-lo->y)/(hi->y-lo->y);
        if (!found || edge_x < row.left) {
            row.left=edge_x;
            row.u=(long long)lo->u*65536
                + (long long)(hi->u-lo->u)*65536*(y-lo->y)/(hi->y-lo->y);
            row.v=(long long)lo->v*65536
                + (long long)(hi->v-lo->v)*65536*(y-lo->y)/(hi->y-lo->y);
            found=1;
        }
    }
    return row;
}

static void triangle(const Vtx *v0, const Vtx *v1, const Vtx *v2, int gouraud, const Tex *t, int semi, int abr) {
    int dith = st.dither && (gouraud || (t != NULL && !t->raw));
    long long area, du_dx = 0, dv_dx = 0;
    int minx, maxx, miny, maxy;
    int tl0, tl1, tl2;
    int x, y;

    minx = v0->x < v1->x ? v0->x : v1->x;
    minx = v2->x < minx ? v2->x : minx;
    maxx = v0->x > v1->x ? v0->x : v1->x;
    maxx = v2->x > maxx ? v2->x : maxx;
    miny = v0->y < v1->y ? v0->y : v1->y;
    miny = v2->y < miny ? v2->y : miny;
    maxy = v0->y > v1->y ? v0->y : v1->y;
    maxy = v2->y > maxy ? v2->y : maxy;
    if (maxx - minx >= 1024 || maxy - miny >= 512) {
        return;
    }
    area = edge(v0, v1, v2->x, v2->y);
    if (area == 0) {
        return;
    }
    if (area < 0) {
        const Vtx *s = v1;
        v1 = v2;
        v2 = s;
        area = -area;
    }
    if (minx < st.clip_x0) minx = st.clip_x0;
    if (miny < st.clip_y0) miny = st.clip_y0;
    if (maxx > st.clip_x1) maxx = st.clip_x1;
    if (maxy > st.clip_y1) maxy = st.clip_y1;
    tl0 = top_left(v1, v2);
    tl1 = top_left(v2, v0);
    tl2 = top_left(v0, v1);
    if (t != NULL) {
        du_dx = uv_gradient(v0, v1, v2, area, 0);
        dv_dx = uv_gradient(v0, v1, v2, area, 1);
    }
    for (y = miny; y <= maxy; y++) {
        GpuTextureRow row = {0, 0, 0};
        if (t != NULL) row = texture_row(v0, v1, v2, y);
        for (x = minx; x <= maxx; x++) {
            long long w0 = edge(v1, v2, x, y);
            long long w1 = edge(v2, v0, x, y);
            long long w2 = edge(v0, v1, x, y);
            int r = v0->r, g = v0->g, b = v0->b, u = 0, v = 0;

            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2)) {
                continue;
            }
            if (gouraud) {
                r = (int)((v0->r * w0 + v1->r * w1 + v2->r * w2) / area);
                g = (int)((v0->g * w0 + v1->g * w1 + v2->g * w2) / area);
                b = (int)((v0->b * w0 + v1->b * w1 + v2->b * w2) / area);
            }
            if (t != NULL) {
                long long offset = (long long)x*65536-row.left;
                u = (int)((row.u + offset*du_dx/65536 + 32768) >> 16);
                v = (int)((row.v + offset*dv_dx/65536 + 32768) >> 16);
            }
            shade(x, y, t, u, v, r, g, b, semi, abr, dith);
        }
    }
}

static void make_tex(Tex *t, uint32_t tpage, uint32_t clut, int raw) {
    t->mode = (tpage >> 7) & 3;
    if (t->mode == 3) {
        t->mode = 2;
    }
    t->tx = (tpage & 0xF) * 64;
    t->ty = ((tpage >> 4) & 1) * 256;
    t->cx = (clut & 0x3F) * 16;
    t->cy = (clut >> 6) & 0x1FF;
    t->raw = raw;
}

static void polygon(const uint32_t *w, int op) {
    int quad = op & 0x08;
    int textured = op & 0x04;
    int semi = op & 0x02;
    int gouraud = op & 0x10;
    int nv = quad ? 4 : 3;
    Vtx v[4];
    uint32_t clut = 0;
    uint32_t tpage = st.tpage;
    uint32_t col = w[0];
    Tex tex;
    int i, k;

    i = 1;
    for (k = 0; k < nv; k++) {
        if (gouraud && k > 0) {
            col = w[i++];
        }
        v[k].r = col & 0xFF;
        v[k].g = (col >> 8) & 0xFF;
        v[k].b = (col >> 16) & 0xFF;
        v[k].x = sign11(w[i] & 0xFFFF) + st.ofs_x;
        v[k].y = sign11(w[i] >> 16) + st.ofs_y;
        i++;
        v[k].u = v[k].v = 0;
        if (textured) {
            v[k].u = w[i] & 0xFF;
            v[k].v = (w[i] >> 8) & 0xFF;
            if (k == 0) {
                clut = w[i] >> 16;
            } else if (k == 1) {
                tpage = w[i] >> 16;
            }
            i++;
        }
    }
    if (textured) {
        /* A textured polygon's page becomes the current draw mode page (GPUSTAT bits 0-8, 11). */
        st.tpage = (st.tpage & ~0x9FFu) | (tpage & 0x9FF);
        make_tex(&tex, tpage, clut, op & 0x01);
    }
    triangle(&v[0], &v[1], &v[2], gouraud, textured ? &tex : NULL, semi, (tpage >> 5) & 3);
    if (quad) {
        triangle(&v[1], &v[2], &v[3], gouraud, textured ? &tex : NULL, semi, (tpage >> 5) & 3);
    }
}

static void rectangle(const uint32_t *w, int op) {
    static const int sizes[4] = { 0, 1, 8, 16 };
    int textured = op & 0x04;
    int semi = op & 0x02;
    int size = sizes[(op >> 3) & 3];
    int r = w[0] & 0xFF, g = (w[0] >> 8) & 0xFF, b = (w[0] >> 16) & 0xFF;
    int x0 = sign11(w[1] & 0xFFFF) + st.ofs_x;
    int y0 = sign11(w[1] >> 16) + st.ofs_y;
    int u0 = 0, v0 = 0;
    int rw, rh, i = 2;
    int x, y;
    Tex tex;

    if (textured) {
        u0 = w[2] & 0xFF;
        v0 = (w[2] >> 8) & 0xFF;
        make_tex(&tex, st.tpage, w[2] >> 16, op & 0x01);
        i = 3;
    }
    if (size == 0) {
        rw = w[i] & 0x3FF;
        rh = (w[i] >> 16) & 0x1FF;
    } else {
        rw = rh = size;
    }
    for (y = 0; y < rh; y++) {
        int py = y0 + y;
        if (py < st.clip_y0 || py > st.clip_y1) {
            continue;
        }
        for (x = 0; x < rw; x++) {
            int px = x0 + x;
            if (px < st.clip_x0 || px > st.clip_x1) {
                continue;
            }
            /* E1 bits 12 / 13: texture x / y flip for rectangles */
            shade(px, py, textured ? &tex : NULL, st.flip_x ? u0 - x : u0 + x, st.flip_y ? v0 - y : v0 + y, r, g, b,
                  semi, (st.tpage >> 5) & 3, 0);
        }
    }
}

/* ---- transfers ---- */

static void soft_fill(int x, int y, int w, int h, uint32_t rgb24) {
    uint16_t c = (uint16_t)(((rgb24 >> 3) & 31) | (((rgb24 >> 11) & 31) << 5) | (((rgb24 >> 19) & 31) << 10));
    int i, j;

    for (j = 0; j < h; j++) {
        uint16_t *row = vram[(y + j) & (GPU_VRAM_H - 1)];
        for (i = 0; i < w; i++) {
            row[(x + i) & (GPU_VRAM_W - 1)] = c;
            pixels_written++;
        }
    }
}

static void soft_load(int x, int y, int w, int h, const uint16_t *src) {
    int i, j;

    for (j = 0; j < h; j++) {
        uint16_t *row = vram[(y + j) & (GPU_VRAM_H - 1)];
        for (i = 0; i < w; i++) {
            uint16_t *d = &row[(x + i) & (GPU_VRAM_W - 1)];
            uint16_t c = *src++;
            if (!st.check_mask || !(*d & 0x8000)) {
                *d = c | (st.set_mask ? 0x8000 : 0);
                pixels_written++;
            }
        }
    }
}

static void soft_store(int x, int y, int w, int h, uint16_t *dst) {
    int i, j;

    for (j = 0; j < h; j++) {
        const uint16_t *row = vram[(y + j) & (GPU_VRAM_H - 1)];
        for (i = 0; i < w; i++) {
            *dst++ = row[(x + i) & (GPU_VRAM_W - 1)];
        }
    }
}

static void soft_move(int sx, int sy, int dx, int dy, int w, int h) {
    static uint16_t line[GPU_VRAM_W];
    int i, j;

    if (w > GPU_VRAM_W) {
        w = GPU_VRAM_W;
    }
    /* Row order chosen so an overlapping copy reads each row before it is overwritten. */
    for (j = 0; j < h; j++) {
        int jj = dy > sy ? h - 1 - j : j;
        const uint16_t *s = vram[(sy + jj) & (GPU_VRAM_H - 1)];
        uint16_t *d = vram[(dy + jj) & (GPU_VRAM_H - 1)];
        for (i = 0; i < w; i++) {
            line[i] = s[(sx + i) & (GPU_VRAM_W - 1)];
        }
        for (i = 0; i < w; i++) {
            uint16_t *p = &d[(dx + i) & (GPU_VRAM_W - 1)];
            if (!st.check_mask || !(*p & 0x8000)) {
                *p = line[i] | (st.set_mask ? 0x8000 : 0);
                pixels_written++;
            }
        }
    }
}

/* Line from a to b (psx-spx "Line Drawing"): both end points drawn, colour interpolated along the
 * longer axis, dithered when Gouraud (E1 bit 9), clipped to the draw area per pixel. A segment
 * of 1024 or more columns or 512 or more rows is not drawn. */
static void line_seg(const Vtx *a, const Vtx *b, int gouraud, int semi, int abr) {
    int dx = b->x - a->x;
    int dy = b->y - a->y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int n = adx > ady ? adx : ady;
    int dith = st.dither && gouraud;
    int i;

    if (adx >= 1024 || ady >= 512) {
        return;
    }
    for (i = 0; i <= n; i++) {
        /* 16.16 fixed point, rounded to the nearest pixel */
        int x = n == 0 ? a->x : a->x + (int)(((long long)dx * i * 65536 / n + 32768) >> 16);
        int y = n == 0 ? a->y : a->y + (int)(((long long)dy * i * 65536 / n + 32768) >> 16);
        int r = a->r, g = a->g, bl = a->b;
        int d;

        if (x < st.clip_x0 || x > st.clip_x1 || y < st.clip_y0 || y > st.clip_y1) {
            continue;
        }
        if (gouraud && n != 0) {
            r = a->r + (b->r - a->r) * i / n;
            g = a->g + (b->g - a->g) * i / n;
            bl = a->b + (b->b - a->b) * i / n;
        }
        d = dith ? dither_tbl[y & 3][x & 3] : 0;
        put(x, y, to5(r, d), to5(g, d), to5(bl, d), semi, abr, 0);
    }
}

/* GP0 40-5F: single lines (3 words, Gouraud 4) and polylines (vertices up to the 0x5xxx5xxx
 * terminator). Lines are never textured. */
static void line(const uint32_t *w, int op) {
    int gouraud = op & 0x10;
    int semi = op & 0x02;
    int abr = (st.tpage >> 5) & 3;
    uint32_t col = w[0];
    Vtx v[2];
    int i = 1, k = 0;

    for (;;) {
        Vtx *p = &v[k & 1];

        if (k > 0 && gouraud) {
            if ((w[i] & 0xF000F000) == 0x50005000 && (op & 0x08)) {
                break;
            }
            col = w[i++];
        }
        if (k > 0 && (op & 0x08) && (w[i] & 0xF000F000) == 0x50005000) {
            break;
        }
        p->r = col & 0xFF;
        p->g = (col >> 8) & 0xFF;
        p->b = (col >> 16) & 0xFF;
        p->x = sign11(w[i] & 0xFFFF) + st.ofs_x;
        p->y = sign11(w[i] >> 16) + st.ofs_y;
        p->u = p->v = 0;
        i++;
        if (k > 0) {
            line_seg(&v[(k - 1) & 1], p, gouraud, semi, abr);
                }
        k++;
        if (!(op & 0x08) && k == 2) {
            break;
        }
    }
}

/* ---- GP0 ---- */

/* Words of the command starting at w[0] (n words available), or 0 when it is cut off. */
static int command_len(const uint32_t *w, int n) {
    int op = w[0] >> 24;

    if (op == 0x02) {
        return 3;
    }
    if (op >= 0x20 && op < 0x40) {
        int nv = (op & 0x08) ? 4 : 3;
        return 1 + nv * ((op & 0x04) ? 2 : 1) + ((op & 0x10) ? nv - 1 : 0);
    }
    if (op >= 0x40 && op < 0x60) {
        int i;
        if (!(op & 0x08)) {
            return (op & 0x10) ? 4 : 3;
        }
        for (i = 3; i < n; i++) { /* polyline: up to the 0x5xxx5xxx terminator */
            if ((w[i] & 0xF000F000) == 0x50005000) {
                return i + 1;
            }
        }
        return 0;
    }
    if (op >= 0x60 && op < 0x80) {
        return 2 + ((op & 0x04) ? 1 : 0) + (((op >> 3) & 3) == 0 ? 1 : 0);
    }
    if (op >= 0x80 && op < 0xA0) {
        return 4;
    }
    if (op >= 0xA0 && op < 0xC0) {
        int cw, ch;
        if (n < 3) {
            return 0;
        }
        cw = (((w[2] & 0xFFFF) - 1) & 0x3FF) + 1;
        ch = (((w[2] >> 16) - 1) & 0x1FF) + 1;
        return 3 + (cw * ch + 1) / 2;
    }
    if (op >= 0xC0 && op < 0xE0) {
        return 3;
    }
    return 1;
}

static void command(const uint32_t *w) {
    uint32_t c = w[0];
    int op = c >> 24;

    if (op >= 0x20 && op < 0x40) {
        polygon(w, op);
    } else if (op >= 0x60 && op < 0x80) {
        rectangle(w, op);
    } else if (op >= 0x40 && op < 0x60) {
        line(w, op);
    } else if (op >= 0x80 && op < 0xA0) {
        soft_move(w[1] & 0x3FF, (w[1] >> 16) & 0x1FF, w[2] & 0x3FF, (w[2] >> 16) & 0x1FF,
                         (((w[3] & 0xFFFF) - 1) & 0x3FF) + 1, (((w[3] >> 16) - 1) & 0x1FF) + 1);
    } else if (op >= 0xA0 && op < 0xC0) {
        soft_load(w[1] & 0x3FF, (w[1] >> 16) & 0x1FF, (((w[2] & 0xFFFF) - 1) & 0x3FF) + 1,
                         (((w[2] >> 16) - 1) & 0x1FF) + 1, (const uint16_t *)&w[3]);
    } else if (op >= 0xC0 && op < 0xE0) {
        log_once(op, "VRAM to CPU in a packet, ignored");
    } else {
        switch (op) {
        case 0x00:
        case 0x01:
            break;
        case 0x02:
            soft_fill(w[1] & 0x3F0, (w[1] >> 16) & 0x1FF, ((w[2] & 0x3FF) + 15) & ~15, (w[2] >> 16) & 0x1FF,
                        c & 0xFFFFFF);
            break;
        case 0xE1:
            st.tpage = c & 0x9FF;
            st.dither = (c >> 9) & 1;
            st.draw_disp = (c >> 10) & 1;
            st.flip_x = (c >> 12) & 1;
            st.flip_y = (c >> 13) & 1;
            break;
        case 0xE2:
            st.tw_mask_x = (c & 31) * 8;
            st.tw_mask_y = ((c >> 5) & 31) * 8;
            st.tw_off_x = ((c >> 10) & 31) * 8;
            st.tw_off_y = ((c >> 15) & 31) * 8;
            break;
        case 0xE3:
            st.clip_x0 = c & 0x3FF;
            st.clip_y0 = (c >> 10) & 0x1FF;
            break;
        case 0xE4:
            st.clip_x1 = c & 0x3FF;
            st.clip_y1 = (c >> 10) & 0x1FF;
            break;
        case 0xE5:
            st.ofs_x = sign11(c & 0x7FF);
            st.ofs_y = sign11((c >> 11) & 0x7FF);
            break;
        case 0xE6:
            st.set_mask = c & 1;
            st.check_mask = (c >> 1) & 1;
            break;
        default:
            log_once(op, "unhandled command, ignored");
            break;
        }
    }
}

static void soft_gp0(const uint32_t *words, int count) {
    int i = 0;

    while (i < count) {
        int len = command_len(&words[i], count - i);

        if (len == 0 || i + len > count) {
            log_once(words[i] >> 24, "command cut off at the packet end, dropped");
            return;
        }
        command(&words[i]);
        i += len;
    }
}

/* ---- display ---- */

void gpu_set_display(int x, int y, int w, int h, int rgb24) {
    disp.x = x;
    disp.y = y;
    disp.w = w;
    disp.h = h;
    disp.rgb24 = rgb24;
}

void gpu_set_display_enable(int on) {
    disp.on = on;
}

int gpu_read_display(uint32_t *out, int *pw, int *ph) {
    int w = disp.w < 1 ? 1 : disp.w > 640 ? 640 : disp.w;
    int h = disp.h < 1 ? 1 : disp.h > 480 ? 480 : disp.h; /* NTSC: at most 480 visible lines */
    int x, y;

    if (!disp.on) {
        return 0;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *row = vram[(disp.y + y) & (GPU_VRAM_H - 1)];
        uint32_t *o = out + y * w;

        if (disp.rgb24) {
            const uint8_t *bytes = (const uint8_t *)row;
            for (x = 0; x < w; x++) {
                int i = disp.x * 2 + x * 3;
                o[x] = ((uint32_t)bytes[i & 2047] << 16) | ((uint32_t)bytes[(i + 1) & 2047] << 8) | bytes[(i + 2) & 2047];
            }
        } else {
            for (x = 0; x < w; x++) {
                uint16_t c = row[(disp.x + x) & (GPU_VRAM_W - 1)];
                uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
                o[x] = (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
            }
        }
    }
    *pw = w;
    *ph = h;
    return 1;
}

void gpu_draw_area_size(int *w, int *h) {
    *w = st.clip_x1 - st.clip_x0 + 1;
    *h = st.clip_y1 - st.clip_y0 + 1;
}


const uint16_t *gpu_vram(void) { return &vram[0][0]; }

GpuWork gpu_gp0(const uint32_t *words, int count)
{
    uint64_t before = pixels_written;
    soft_gp0(words, count);
    return (GpuWork){count > 0 ? (uint64_t)count : 0, pixels_written - before};
}
GpuWork gpu_fill(int x, int y, int w, int h, uint32_t rgb24)
{
    uint64_t before = pixels_written;
    soft_fill(x, y, w, h, rgb24);
    return (GpuWork){0, pixels_written - before};
}
GpuWork gpu_load(int x, int y, int w, int h, const uint16_t *src)
{
    uint64_t before = pixels_written;
    soft_load(x, y, w, h, src);
    return (GpuWork){w > 0 && h > 0 ? ((uint64_t)w * h + 1) / 2 : 0,
                     pixels_written - before};
}
GpuWork gpu_store(int x, int y, int w, int h, uint16_t *dst)
{
    soft_store(x, y, w, h, dst);
    return (GpuWork){w > 0 && h > 0 ? ((uint64_t)w * h + 1) / 2 : 0, 0};
}
GpuWork gpu_move(int sx, int sy, int dx, int dy, int w, int h)
{
    uint64_t before = pixels_written;
    soft_move(sx, sy, dx, dy, w, h);
    return (GpuWork){0, pixels_written - before};
}
