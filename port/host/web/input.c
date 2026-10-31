/* Live input on the web (the native twin is port/host/sdl/input.c). The Shell samples keyboard and
 * gamepads on the main thread and publishes the wire-order button mask; the game thread latches
 * it once per VBlank, so a sample reaches PadRead at the first VBlank after it was written. */
#include "input.h"
#include <emscripten.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "../plat.h"
#include "../../psyq/clock.h"
#include "../../psyq/crt.h"

extern void port_pad_latch(uint16_t wire);
extern uint32_t port_pad_value(void);
extern int port_pad_latch_applies(void);

/* u32 words shared with the Shell (inputControls in port/web/dev/index.html uses these indices):
 * mask and sample number (main thread: mask first, then the number), then the number, VBlank and
 * PadRead value of the last latch (game thread). */
enum { MASK, SEQ, LATCHED_SEQ, LATCHED_VBLANK, PAD_VALUE, WORDS };
static _Atomic uint32_t block[WORDS];
EMSCRIPTEN_KEEPALIVE void *web_input_address(void) { return block; }
typedef struct { uint32_t vblank; uint16_t mask; } ReplayEvent;
static ReplayEvent *events;
static size_t event_count, next_event;
static int replay;
static uint16_t replay_mask;
int web_input_replay(const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[128], k[64], mask[64], extra, *end;
    int failed = 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%63s %63s %c", k, mask, &extra) != 2) { failed = 1; break; }
        errno = 0;
        unsigned long vblank = strtoul(k, &end, 10);
        if (errno || k[0] < '0' || k[0] > '9' || *end || vblank > UINT32_MAX ||
            (event_count && vblank <= events[event_count-1].vblank)) { failed = 1; break; }
        errno = 0;
        unsigned long value = strtoul(mask, &end, 16);
        if (errno || mask[0] == '-' || mask[0] == '+' || *end || value > UINT16_MAX) { failed = 1; break; }
        ReplayEvent *next = realloc(events, (event_count + 1) * sizeof(*events));
        if (!next) { failed = 1; break; }
        events = next;
        events[event_count++] = (ReplayEvent){vblank, value};
    }
    failed |= ferror(file) != 0;
    failed |= fclose(file) != 0;
    replay = !failed;
    return failed ? -1 : 0;
}
void plat_input_vblank(void)
{
    /* Scene captures replay no live input, as in native-headless. */
    if (port_capture_active()) return;
    if (replay) {
        while (next_event < event_count && events[next_event].vblank <= clock_vblanks())
            replay_mask = events[next_event++].mask;
        port_pad_latch(replay_mask);
        return;
    }
    uint32_t seq = atomic_load(&block[SEQ]);
    /* A VBlank whose poll PsyQ skipped keeps the previous packet: that sample was not consumed. */
    int applies = port_pad_latch_applies();
    port_pad_latch((uint16_t)atomic_load(&block[MASK]));
    if (!applies) return;
    atomic_store(&block[PAD_VALUE], port_pad_value());
    atomic_store(&block[LATCHED_VBLANK], clock_vblanks());
    atomic_store(&block[LATCHED_SEQ], seq);
}
