/* libetc: VSync and interrupt callbacks over the virtual clock (AD-8).
 * asm:asm/us/main/psyq/libetc.s (mw_lib41) */
#include <stddef.h>

#include "clock.h"
#include "port.h"
#include "psyq.h"

#define VSYNC_SLOTS 8
#define IRQ_COUNT 11

/* Frame-end hook of the runtime (crt.c); absent in unit tests. */
__attribute__((weak)) void port_frame_done(void);

int Vcount; /* VBlanks counted by the VBlank trap; read by VSync(-1) */
static int intr_started; /* asm: intrEnv+0 -- startIntr already ran */
static int pre_vcount;
static uint64_t hcount_cycle;
static void (*vsync_callbacks[VSYNC_SLOTS])(void);
static void (*irq_callbacks[IRQ_COUNT])(void);

/* asm: config/us/bss.yaml, libetc at 0x80135450; both words start at zero. */
static uint32_t pad_buf;
/* asm: asm/us/main/data/psyq/libetc.data.s: _etc_vmode starts at zero.
 * DW1 USA reaches no setter. */
static long video_mode;
/* asm: asm/us/main/psyq/libetc.s: GetVideoMode. */
long GetVideoMode(void)
{
    clock_psyq_entry();
    return video_mode;
}
static int PadIdentifier __attribute__((unused)); /* Stored by PsyQ. */

/* asm:asm/us/main/psyq/libetc.s:PadInit -- the scene observer reapplies held inputs at VBlank. */
void PadInit(int mode)
{
	clock_psyq_entry();
	PadIdentifier = mode;
	pad_buf = 0xFFFFFFFFu;
	ResetCallback();
}

/* PCSX-Redux overrides use wire bit order; PadRead swaps the two packet bytes. */
void port_pad_latch(uint16_t wire_buttons)
{
	pad_buf = ~((uint32_t)((wire_buttons << 8) | (wire_buttons >> 8)) & 0xffffu);
}

/* asm:asm/us/main/psyq/libetc.s:PadRead -- first controller, active-high PsyQ byte order. */
unsigned long PadRead(int id)
{
	clock_psyq_entry();
	(void)id;
	return ~pad_buf;
}

/* asm: trapIntrVSync -- Vcount++, then the 8 callback slots in order. */
static void trap_vsync(void)
{
	Vcount++;
	for (int i = 0; i < VSYNC_SLOTS; i++) {
		if (vsync_callbacks[i])
			vsync_callbacks[i]();
	}
}

/* Called by the port when an interrupt line other than VBlank fires (root counters, CD, ...). */
void psyq_raise_irq(int irq)
{
	if (irq > 0 && irq < IRQ_COUNT && irq_callbacks[irq])
		irq_callbacks[irq]();
}

static int vcount_reached(void *target)
{
	return Vcount >= *(int *)target;
}

/* asm: v_wait -- waits while Vcount < target. The timeout path ("VSync: timeout") cannot happen:
 * the wait jumps to the next VBlank. */
static void v_wait(int target)
{
	clock_wait_until(vcount_reached, &target);
}

static int lines_since(uint64_t cycle)
{
	return (int)clock_lines(clock_now() - cycle);
}

/* asm: VSync. mode < 0: VBlank count; 1: lines since the last return; 0: next VBlank; n > 1: VBlank
 * n after the last return. The interlaced-field wait of the asm is left out until GP1 status
 * exists (story 2.12); DW1 runs 320x240 non-interlaced. */
int VSync(int mode)
{
	clock_psyq_entry();
	int count = lines_since(hcount_cycle) & 0xFFFF;
	if (mode < 0)
		return Vcount;
	if (mode == 1)
		return count;
	/* Without ResetCallback there is no VBlank trap and Vcount never moves (the console would
	 * print "VSync: timeout" after a while). */
	PORT_ASSERT(intr_started);
	v_wait(mode > 0 ? pre_vcount - 1 + mode : pre_vcount);
	v_wait(Vcount + 1);
	pre_vcount = Vcount;
	hcount_cycle = clock_now();
	if (port_frame_done)
		port_frame_done();
	return count;
}

/* asm: VSyncCallbacks -- slot ch of the VBlank trap; returns the previous function. */
long VSyncCallbacks(int ch, void (*f)())
{
	clock_psyq_entry();
	if (ch < 0 || ch >= VSYNC_SLOTS)
		return 0;
	void (*old)(void) = vsync_callbacks[ch];
	vsync_callbacks[ch] = (void (*)(void))f;
	return (long)(uintptr_t)old;
}

/* asm: VSyncCallback -- VSyncCallbacks(4, f). */
int VSyncCallback(void (*f)())
{
	return (int)VSyncCallbacks(4, f);
}

/* asm: InterruptCallback -- callback of interrupt line irq; returns the previous function. Line 0
 * (VBlank) is served by the VBlank trap and its slots, as with libetc's own handler. */
long InterruptCallback(int irq, void (*f)())
{
	clock_psyq_entry();
	PORT_ASSERT(irq != 0); /* line 0 (VBlank) belongs to the VBlank trap */
	if (irq <= 0 || irq >= IRQ_COUNT)
		return 0;
	void (*old)(void) = irq_callbacks[irq];
	irq_callbacks[irq] = (void (*)(void))f;
	return (long)(uintptr_t)old;
}

/* asm: ResetCallback -> startIntr. The first call clears the interrupt environment (no
 * callbacks) and starts the VBlank trap, returning non-zero (&intrEnv on the console); later
 * calls change nothing and return 0. */
int ResetCallback(void)
{
	clock_psyq_entry();
	if (intr_started)
		return 0;
	for (int i = 0; i < VSYNC_SLOTS; i++)
		vsync_callbacks[i] = NULL;
	for (int i = 0; i < IRQ_COUNT; i++)
		irq_callbacks[i] = NULL;
	clock_set_vblank_handler(trap_vsync);
	intr_started = 1;
	return 1;
}

/* Back to the state before boot (tests). */
void psyq_libetc_reset(void)
{
	pad_buf = 0;
	PadIdentifier = 0;
	Vcount = pre_vcount = 0;
	hcount_cycle = 0;
	intr_started = 0;
	clock_set_vblank_handler(NULL);
}
