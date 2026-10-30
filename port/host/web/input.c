/* Live input on the web (the native twin is port/host/sdl/input.c). The Shell samples keyboard and
 * gamepads on the main thread and publishes the wire-order button mask; the game thread latches
 * it once per VBlank, so a sample reaches PadRead at the first VBlank after it was written. */
#include "input.h"
#include <emscripten.h>
#include <stdatomic.h>
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
void plat_input_vblank(void)
{
    /* Scene captures replay no live input, as in native-headless. */
    if (port_capture_active()) return;
    uint32_t seq = atomic_load(&block[SEQ]);
    /* A VBlank whose poll PsyQ skipped keeps the previous packet: that sample was not consumed. */
    int applies = port_pad_latch_applies();
    port_pad_latch((uint16_t)atomic_load(&block[MASK]));
    if (!applies) return;
    atomic_store(&block[PAD_VALUE], port_pad_value());
    atomic_store(&block[LATCHED_VBLANK], clock_vblanks());
    atomic_store(&block[LATCHED_SEQ], seq);
}
