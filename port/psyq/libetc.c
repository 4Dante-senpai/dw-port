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
__attribute__((weak)) uint32_t bios_card_vblank_cpu_cycles(void);
__attribute__((weak)) void port_vsync_return(void);
__attribute__((weak)) void port_scene_movie_timing(const char *event, unsigned value);

int Vcount; /* VBlanks counted by the VBlank trap; read by VSync(-1) */
static int intr_started; /* asm: intrEnv+0 -- startIntr already ran */
static int pre_vcount;
static uint64_t hcount_cycle;
static void (*vsync_callbacks[VSYNC_SLOTS])(void);
static void (*irq_callbacks[IRQ_COUNT])(void);
static struct { uint32_t verifier, entry, tail; } irq_cpu[IRQ_COUNT];
static int pad_started;
static int pad_poll_missed;

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
	if (mode == 0) clock_advance_cost(CLOCK_PAD_INIT_CPU_CYCLES, CLOCK_COST_CPU);
	pad_started = 1;
}

/* PCSX-Redux overrides use wire bit order; PadRead swaps the two packet bytes. */
void port_pad_latch(uint16_t wire_buttons)
{
	/* Keep the wire input at its original VBlank. OpenBIOS only publishes a
	 * packet when its SIO verifier sees VBlank. A VBlank arriving after that
	 * check, inside a timer callback, is ACKed by PsyQ's inner IRQ loop and
	 * leaves the previously published packet intact. */
	if (!pad_started || !pad_poll_missed)
		pad_buf = ~((uint32_t)((wire_buttons << 8) | (wire_buttons >> 8)) & 0xffffu);
	pad_poll_missed = 0;
}

/* What PadRead would return, without its cost: lets the host observe the latched pad. */
uint32_t port_pad_value(void)
{
	return ~pad_buf;
}

/* asm:asm/us/main/psyq/libetc.s:PadRead -- first controller, active-high PsyQ byte order. */
unsigned long PadRead(int id)
{
	clock_psyq_entry();
	(void)id;
	if (port_scene_movie_timing) port_scene_movie_timing("PadRead", ~pad_buf);
	return ~pad_buf;
}

/* asm: trapIntrVSync -- Vcount++, then the 8 callback slots in order. */
static void trap_vsync(void)
{
	/* The BIOS SIO verifier has already been passed when a timer IRQ spans
	 * VBlank. In that case PsyQ ACKs this VBlank without polling the pad. */
	if (!pad_poll_missed) {
		uint32_t cpu = CLOCK_VBLANK_NOPAD_CPU_CYCLES;
		if (pad_started) cpu += CLOCK_VBLANK_PAD_CPU_CYCLES + CLOCK_VBLANK_PAD_ROUTE_CPU_CYCLES;
		if (bios_card_vblank_cpu_cycles) cpu += bios_card_vblank_cpu_cycles();
		clock_advance_cost(cpu, CLOCK_COST_CPU);
	}
	Vcount++;
	for (int i = 0; i < VSYNC_SLOTS; i++) {
		if (vsync_callbacks[i])
			vsync_callbacks[i]();
	}
}

/* Called by the port when an interrupt line other than VBlank fires (root counters, CD, ...). */
void psyq_raise_irq(int irq)
{
	if (irq > 0 && irq < IRQ_COUNT && irq_callbacks[irq]) {
		uint64_t start = clock_now();
		uint64_t vb = clock_vblank_cycle(clock_vblanks() + 1);
		clock_advance_cost(irq_cpu[irq].entry, CLOCK_COST_CPU);
		irq_callbacks[irq]();
		uint64_t ack_check = clock_now() + CLOCK_SOUND_IRQ_ACK_CHECK_CYCLES;
		clock_advance_cost(irq_cpu[irq].tail, CLOCK_COST_CPU);
		if (irq_cpu[irq].entry && vb > start + irq_cpu[irq].verifier && vb <= ack_check)
			pad_poll_missed = 1;
		if (pad_poll_missed && getenv("DW1_DEVICE_PROBE")) fprintf(stderr, "pad-miss vb=%u cycle=%llu\n", clock_vblanks()+1, (unsigned long long)start);
	}
}

void psyq_irq_cpu_cost(int irq, uint32_t verifier, uint32_t entry, uint32_t tail)
{
	PORT_ASSERT(irq > 0 && irq < IRQ_COUNT);
	irq_cpu[irq].verifier = verifier;
	irq_cpu[irq].entry = entry;
	irq_cpu[irq].tail = tail;
}

/* asm: v_wait -- polls Vcount at the original loop branches. The clock
 * always delivers VBlank, so the retail hardware-timeout path is unreachable. */
static void v_wait(int target)
{
    if (Vcount >= target) {
        clock_poll_branch(CLOCK_VSYNC_WAIT_DONE_ENTER_CYCLES);
        clock_poll_branch(CLOCK_VSYNC_WAIT_EPILOGUE_CYCLES);
        return;
    }
    clock_poll_branch(CLOCK_VSYNC_WAIT_FIRST_BRANCH_CYCLES);
    for (;;) {
        if (Vcount >= target) {
            clock_poll_branch(CLOCK_VSYNC_WAIT_RETURN_CHECK_CYCLES);
            return;
        }
        clock_poll_branch(CLOCK_VSYNC_WAIT_CHECK_BRANCH_CYCLES);
        clock_poll_branch(CLOCK_VSYNC_WAIT_LOOP_BRANCH_CYCLES);
    }
}

static void stable_counter_branch(unsigned finish)
{
    uint64_t t = clock_now();
    unsigned first = CLOCK_CD_POLL_T1_FIRST_CYCLES;
    unsigned second = CLOCK_CD_POLL_T1_SECOND_CYCLES;
    unsigned retry = CLOCK_CD_POLL_T1_FIRST_RETRY_CYCLES;
    unsigned prefix = CLOCK_VSYNC_COUNTER_PREFIX_CYCLES;
    while (clock_lines(t + first - CLOCK_CD_POLL_T1_PHASE_CYCLES) !=
           clock_lines(t + second - CLOCK_CD_POLL_T1_PHASE_CYCLES)) {
        clock_poll_branch(retry);
        t = clock_now();
        first = CLOCK_CD_POLL_T1_RETRY_FIRST_CYCLES;
        second = CLOCK_CD_POLL_T1_RETRY_SECOND_CYCLES;
        retry = CLOCK_CD_POLL_T1_RETRY_CYCLES;
        finish -= prefix;
        prefix = 0;
    }
    clock_poll_branch(finish);
}
void psyq_vsync_query_cost(void)
{
    stable_counter_branch(CLOCK_CD_POLL_VSYNC_JUMP_CYCLES);
    clock_poll_branch(CLOCK_CD_POLL_VSYNC_RETURN_CYCLES);
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
	if (mode < 0 && !clock_in_handler()) psyq_vsync_query_cost();
    else if (mode == 0) {
        stable_counter_branch(CLOCK_VSYNC_ZERO_MODE_BRANCH_CYCLES);
        clock_poll_branch(CLOCK_VSYNC_ZERO_TARGET_BRANCH_CYCLES);
        clock_poll_branch(CLOCK_VSYNC_ZERO_WAIT_BRANCH_CYCLES);
        clock_poll_branch(CLOCK_CD_POLL_CALL_CPU_CYCLES);
    } else clock_psyq_entry();
	int count = lines_since(hcount_cycle) & 0xFFFF;
	if (mode < 0 || mode == 1) {
		if (port_vsync_return) port_vsync_return();
		return mode < 0 ? Vcount : count;
	}
	/* Without ResetCallback there is no VBlank trap and Vcount never moves (the console would
	 * print "VSync: timeout" after a while). */
	PORT_ASSERT(intr_started);
	v_wait(mode > 0 ? pre_vcount - 1 + mode : pre_vcount);
    clock_poll_branch(CLOCK_VSYNC_BETWEEN_WAITS_CPU_CYCLES);
	v_wait(Vcount + 1);
	clock_advance_cost(CLOCK_VSYNC_TAIL_CPU_CYCLES, CLOCK_COST_CPU);
	pre_vcount = Vcount;
	hcount_cycle = clock_now();
    if (getenv("DW1_VSYNC_PROBE")) fprintf(stderr, "vsync-return %d %llu\n", Vcount, (unsigned long long)clock_now());
	if (port_vsync_return) port_vsync_return();
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
	psyq_irq_cpu_cost(irq, 0, 0, 0);
	return (long)(uintptr_t)old;
}

/* asm: ResetCallback -> startIntr. The first call clears the interrupt environment (no
 * callbacks) and starts the VBlank trap, returning non-zero (&intrEnv on the console); later
 * calls change nothing and return 0. */
int ResetCallback(void)
{
	clock_advance_cost(intr_started ? CLOCK_IRQ_SETUP_REPEAT_CYCLES : CLOCK_IRQ_SETUP_CYCLES,
	                   CLOCK_COST_CPU);
	if (intr_started)
		return 0;
	for (int i = 0; i < VSYNC_SLOTS; i++)
		vsync_callbacks[i] = NULL;
	for (int i = 0; i < IRQ_COUNT; i++) {
		irq_callbacks[i] = NULL;
		irq_cpu[i].verifier = irq_cpu[i].entry = irq_cpu[i].tail = 0;
	}
	clock_set_vblank_handler(trap_vsync);
	intr_started = 1;
	return 1;
}

/* Back to the state before boot (tests). */
void psyq_libetc_reset(void)
{
	pad_buf = 0;
	pad_started = pad_poll_missed = 0;
	PadIdentifier = 0;
	Vcount = pre_vcount = 0;
	hcount_cycle = 0;
	intr_started = 0;
	clock_set_vblank_handler(NULL);
}
