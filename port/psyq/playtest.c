/* Canonical SHA-256 chain: previous digest, display hash, little-endian scalar fields.
 * Observation never calls a timed PsyQ API or writes VRAM. */
#include "playtest.h"
#include "clock.h"
#include "../backend/gpu.h"
#include "../host/pack/sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
static FILE *output;
static unsigned long every;
static uint32_t origin, last, overlay, rand_count, rand_last;
static uint16_t pad;
static int started, active, sampled;
static unsigned char chain[32];
static char digest_text[65], display_text[65];
/* Keep observer buffers outside the statically linked 24-bit OT window. */
static uint32_t *pixels;
static unsigned char *rgb555;
static void hex(const unsigned char *bytes, char *text)
{
    for (unsigned i = 0; i < 32; i++) sprintf(text + 2*i, "%02x", bytes[i]);
}
static void number(Sha256 *hash, uint32_t value)
{
    unsigned char bytes[] = {value, value >> 8, value >> 16, value >> 24};
    Sha256_Update(hash, bytes, sizeof(bytes));
}
static void write_line(void)
{
    if (output && (fprintf(output, "%u %u %s %s\n", last, overlay, display_text, digest_text) < 0 || fflush(output))) {
        perror("playtest: digest"); exit(1);
    }
}
void port_playtest_prepare(const PortCapture *config)
{
    output = NULL; active = config->digest != NULL;
    every = config->digest_every ? config->digest_every : 60;
    started = sampled = 0; origin = last = overlay = rand_count = rand_last = pad = 0;
    memset(chain, 0, sizeof(chain)); hex(chain, digest_text);
    if (active) {
        pixels = malloc(640 * 480 * sizeof(*pixels));
        rgb555 = malloc(640 * 480 * 2);
        if (!pixels || !rgb555) { fprintf(stderr, "playtest: cannot allocate display buffers\n"); exit(1); }
        output = fopen(config->digest, "w");
        if (!output) { perror("playtest: open digest"); exit(2); }
    }
}
void port_playtest_origin(void) { if (!started) { started = 1; origin = clock_vblanks(); } }
int port_playtest_started(void) { return started; }
uint32_t port_playtest_vblank(void) { return started ? clock_vblanks() - origin : 0; }
void port_playtest_pad(uint16_t mask) { pad = mask; }
void port_playtest_rand(int value) { if (active) { rand_count++; rand_last = (uint32_t)value; } }
void port_playtest_overlay(unsigned value)
{
    overlay = value;
    if (active) fprintf(stderr, "playtest: overlay %u at VBlank %u\n", value, port_playtest_vblank());
}
const char *port_playtest_digest(void) { return digest_text; }
void port_playtest_tick(void)
{
    if (!active || !started) return;
    uint32_t k = port_playtest_vblank();
    if (sampled && k == last) return;
    Sha256 hash;
    unsigned char display[32];
    int w = 0, h = 0;
    int enabled = gpu_read_display(pixels, &w, &h);
    Sha256_Init(&hash);
    number(&hash, enabled); number(&hash, w); number(&hash, h);
    if (enabled) {
        /* RGB555, two explicit bytes per pixel, including RGB24 display mode. */
        for (int i = 0; i < w*h; i++) {
            uint32_t rgb = pixels[i];
            unsigned value = ((rgb >> 19) & 31) | ((rgb >> 6) & 992) | ((rgb << 7) & 31744);
            rgb555[2*i] = value; rgb555[2*i+1] = value >> 8;
        }
        Sha256_Update(&hash, rgb555, (size_t)w*h*2);
    }
    Sha256_Final(&hash, display); hex(display, display_text);
    /* A disabled display has a distinct sentinel for the report. */
    if (!enabled) strcpy(display_text, "off");
    Sha256_Init(&hash); Sha256_Update(&hash, chain, 32); Sha256_Update(&hash, display, 32);
    number(&hash, k); number(&hash, pad); number(&hash, rand_count); number(&hash, rand_last); number(&hash, overlay);
    Sha256_Final(&hash, chain); hex(chain, digest_text);
    last = k; sampled = 1;
    if (k % every == 0) {
        write_line();
#ifdef __EMSCRIPTEN__
        MAIN_THREAD_EM_ASM({ Module.playtestSnapshot = ({vblank: $1, digest: UTF8ToString($0), inputs: Module.recordedInputs || ""}); }, digest_text, last);
#endif
    }
}
void port_playtest_finish(void)
{
    if (active && sampled) {
        if (last % every) write_line();
#ifdef __EMSCRIPTEN__
        MAIN_THREAD_EM_ASM({ Module.playtestDigest = UTF8ToString($0); Module.playtestSnapshot = ({vblank: $1, digest: Module.playtestDigest, inputs: Module.recordedInputs || ""}); }, digest_text, last);
#endif
    }
    if (output && fclose(output)) { perror("playtest: close digest"); exit(1); }
    free(pixels); free(rgb555); pixels = NULL; rgb555 = NULL;
    output = NULL; active = 0;
}
