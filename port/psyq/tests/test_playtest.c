/* L1: stable canonical digest and observational purity. */
#include "../playtest.h"
#include "../clock.h"
#include "../../backend/gpu.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int failures, sparse;
static char shown[65];
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); failures++; } } while (0)
static void run(char out[65], int variant, const char *path, unsigned every)
{
    clock_reset(); gpu_reset(1);
    gpu_fill(0, 0, 320, 240, 0);
    gpu_set_display(0, 0, 2, 2, 0); gpu_set_display_enable(1);
    PortCapture config = {.state_fd = -1, .input_fd = -1, .digest = path, .digest_every = every, .digest_sparse = sparse};
    port_playtest_prepare(&config);
    /* Nonzero frame0 exercises the shared relative origin. */
    clock_advance(clock_vblank_cycle(3));
    port_playtest_origin();
    CHECK(port_playtest_vblank() == 0);
    if (variant == 1) gpu_fill(0, 0, 1, 1, 255);
    port_playtest_pad(variant == 2 ? 8 : 0);
    if (variant == 3) port_playtest_rand(0);
    if (variant == 4) port_playtest_overlay(2);
    if (variant == 5) gpu_set_display_enable(0);
    if (variant == 6) port_playtest_rand(1);
    for (unsigned i = 1; i <= 3; i++) {
        clock_advance(clock_vblank_cycle(1));
        uint64_t before = clock_now();
        uint16_t vram[4];
        gpu_store(0, 0, 2, 2, vram);
        port_playtest_tick();
        CHECK(clock_now() == before);
        CHECK(port_playtest_vblank() == i);
        uint16_t after[4]; gpu_store(0, 0, 2, 2, after);
        CHECK(!memcmp(vram, after, sizeof(vram)));
        char once[65]; strcpy(once, port_playtest_digest());
        port_playtest_tick(); CHECK(!strcmp(once, port_playtest_digest()));
    }
    port_playtest_finish();
    strcpy(out, port_playtest_digest());
    FILE *file = fopen(path, "r");
    unsigned k, overlay; char display[65], digest[65];
    CHECK(file != NULL);
    if (file) {
        for (unsigned expected = every; expected < 3; expected += every)
            CHECK(fscanf(file, "%u %u %64s %64s", &k, &overlay, display, digest) == 4 && k == expected);
        CHECK(fscanf(file, "%u %u %64s %64s", &k, &overlay, display, digest) == 4 && k == 3);
        CHECK(fgetc(file) == '\n' && fgetc(file) == EOF);
        CHECK(!strcmp(out, digest)); strcpy(shown, display); fclose(file);
    }
}
/* The input ring keeps the newest 32 changes, oldest first, and prepare resets it. */
static void ring(void)
{
    clock_reset();
    PortCapture config = {.state_fd = -1, .input_fd = -1};
    port_playtest_prepare(&config);
    port_playtest_origin();
    for (unsigned i = 1; i <= 40; i++) { port_playtest_pad((uint16_t)i); clock_advance(clock_vblank_cycle(1)); }
    uint32_t k[PORT_PLAYTEST_RING]; uint16_t mask[PORT_PLAYTEST_RING];
    CHECK(port_playtest_recent(PORT_PLAYTEST_RING, k, mask) == PORT_PLAYTEST_RING);
    for (unsigned i = 0; i < PORT_PLAYTEST_RING; i++) CHECK(mask[i] == 9 + i && k[i] == 8 + i);
    CHECK(port_playtest_recent(3, k, mask) == 3 && mask[0] == 38 && mask[2] == 40);
    port_playtest_prepare(&config);
    CHECK(port_playtest_recent(PORT_PLAYTEST_RING, k, mask) == 0);
}
int main(void)
{
    ring();
    char path[128]; snprintf(path, sizeof(path), "/tmp/dw1-playtest-%ld.digest", (long)getpid());
    char baseline[65], again[65];
    run(baseline, 0, path, 2); run(again, 0, path, 2); CHECK(!strcmp(baseline, again));
    run(again, 0, path, 1); CHECK(!strcmp(baseline, again));
    run(again, 0, path, 60); CHECK(!strcmp(baseline, again));
    for (int i = 1; i <= 5; i++) { run(again, i, path, 2); CHECK(strcmp(baseline, again)); }
    run(baseline, 3, path, 2); run(again, 6, path, 2); CHECK(strcmp(baseline, again));
    /* Sparse: same lines and screen column, a different but repeatable chain; dense is unchanged. */
    run(baseline, 0, path, 2); char dense[65]; strcpy(dense, shown);
    sparse = 1;
    run(again, 0, path, 2); CHECK(strcmp(baseline, again)); CHECK(!strcmp(dense, shown));
    run(baseline, 0, path, 2); CHECK(!strcmp(baseline, again));
    run(baseline, 0, path, 1); CHECK(strcmp(baseline, again));
    run(baseline, 1, path, 2); CHECK(strcmp(baseline, again));
    sparse = 0;
    run(baseline, 0, path, 2); run(again, 0, path, 2); CHECK(!strcmp(baseline, again));
    unlink(path);
    return failures != 0;
}
