/* Wall pacing of the game thread at each VSync that presents a frame (the native model, port/host/sdl/video.c),
 * plus the Shell's pause: the game thread is held inside VSync, so no VBlank is dispatched. */
#include "pace.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <stdatomic.h>
#include <stdio.h>
#include "../../psyq/clock.h"
#include "../../psyq/crt.h"

/* i32 words shared with the Shell (pauseControls in port/web/dev/index.html uses these indices):
 * pause (main thread: 1 = hold at the next VSync), held (1 while the game thread waits), VBlanks
 * since the anchor, resynchronizations, pauses taken, and stall (main thread, tests only:
 * milliseconds the next frame takes on the wall, to exercise the late path). */
enum { PAUSE, HELD, VBLANKS, RESYNCHRONIZATIONS, PAUSES, STALL, WORDS };
static _Atomic uint32_t control[WORDS];
static uint64_t deadline, remainder;
static uint32_t anchor_vblank, last_vblank;
static int anchored;
EMSCRIPTEN_KEEPALIVE void *web_control_address(void) { return control; }
static uint64_t now_ns(void) { return (uint64_t)(emscripten_get_now() * 1e6); }
static void hold(void)
{
    atomic_store(&control[HELD], 1);
    atomic_fetch_add(&control[PAUSES], 1);
    while (atomic_load(&control[PAUSE]))
        emscripten_futex_wait((void *)&control[PAUSE], 1, __builtin_inf());
    atomic_store(&control[HELD], 0);
    /* The paused wall time is not owed: restart the deadline instead of resynchronizing. */
    deadline = now_ns();
    remainder = 0;
}
void web_pace(void)
{
    if (port_capture_active()) return;
    uint32_t stall = atomic_exchange(&control[STALL], 0);
    for (double until = emscripten_get_now() + stall, left; stall && (left = until - emscripten_get_now()) > 0;)
        emscripten_futex_wait((void *)&control[STALL], 0, left);
    uint32_t vblank = clock_vblanks();
    uint64_t now = now_ns();
    if (!anchored) {
        anchored = 1;
        anchor_vblank = last_vblank = vblank;
        deadline = now;
    } else {
        remainder += (uint64_t)(vblank - last_vblank) * CLOCK_VBLANK_NUM * 1000000000ull;
        last_vblank = vblank;
        deadline += remainder / CLOCK_VBLANK_DEN; remainder %= CLOCK_VBLANK_DEN;
        if (now > deadline && now - deadline > 4ull * CLOCK_VBLANK_NUM * 1000000000ull / CLOCK_VBLANK_DEN) {
            atomic_fetch_add(&control[RESYNCHRONIZATIONS], 1);
            fprintf(stderr, "dw1: pacing resynchronization vblank=%u late_ns=%llu\n",
                    vblank - anchor_vblank, (unsigned long long)(now - deadline));
            deadline = now;
            remainder = 0;
        }
    }
    atomic_store(&control[VBLANKS], vblank - anchor_vblank);
    while ((now = now_ns()) < deadline && !atomic_load(&control[PAUSE]))
        emscripten_futex_wait((void *)&control[PAUSE], 0, (deadline - now) / 1e6);
    if (atomic_load(&control[PAUSE])) hold();
}
