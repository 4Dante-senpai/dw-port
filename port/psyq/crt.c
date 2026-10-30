/* Port runtime start (replaces src/main/_psstart.c, CodeWarrior's crt; AD-1, AD-5, AD-6). */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/gpu.h"
#include "../backend/spu.h"
#include "psyq.h"
#include "../host/plat.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include "../host/web/io_mailbox.h"
#include <emscripten/threading.h>
#endif

#include "clock.h"
#include "crt.h"
#include "scene.h"
#include "dw1_rts_info.h"
#include "overlay.h"
#include "port.h"
#include "ps1mem.h"

/* The CodeWarrior linker words of __rts_info_t__.h, with their PS1 values (initializeHeap). */
unsigned long dw1_rts_end = DW1_RTS_END;
unsigned long dw1_rts_stack_addr = DW1_RTS_STACK_ADDR;
unsigned long dw1_rts_stack_size = DW1_RTS_STACK_SIZE;

int dw1_game_main(void); /* the game's main (src/main/main.c, renamed by CMake) */
#ifdef __EMSCRIPTEN__
#define _end __heap_base
#endif
extern char _end[];      /* end of the image (.bss), from the linker */

/* Static so it lies in the OT window: primitives on the game's stack can be linked (AD-5). */
#define GAME_STACK_SIZE (4u << 20)
static uint8_t game_stack[GAME_STACK_SIZE] __attribute__((aligned(16)));

static long frame_limit;
static int exit_on_overlay;
static long frames;
static long watchdog_frames;

static PortCapture capture_config;
int port_capture_active(void)
{
    return capture_config.mark || capture_config.capture || capture_config.audio_segments;
}

static int marked;
static uint32_t mark_vblank;
static FILE *clock_trace;
#ifndef __EMSCRIPTEN__
static uint32_t frame0_vblank;
static int have_frame0;
#endif

void port_scene_mark(const char *name)
{
	port_trace("mark %s", name);
#ifdef __EMSCRIPTEN__
	MAIN_THREAD_EM_ASM({ (Module.marks ||= []).push(UTF8ToString($0)); }, name);
#endif
	if (!marked && capture_config.mark && !strcmp(name, capture_config.mark)) {
		marked = 1;
		mark_vblank = clock_vblanks();
		port_scene_start();
	}
}

static void trace_event(uint64_t cycle, int kind, void *arg)
{
	(void)arg;
	if (fprintf(clock_trace, "%llu %d\n", (unsigned long long)cycle, kind) < 0) {
		fprintf(stderr, "port: cannot write clock trace\n");
		exit(1);
	}
}

static void capture_vblank(void)
{
	spu_sync();
	port_scene_tick();
#ifndef __EMSCRIPTEN__
	if (have_frame0 && capture_config.vblank_limit &&
	    clock_vblanks() - frame0_vblank >= capture_config.vblank_limit) {
		if (plat_native_wall_finish) plat_native_wall_finish();
		printf("port: reached VBlank %u after frame 0\n", clock_vblanks() - frame0_vblank);
		exit(0);
	}
#endif
#ifdef __EMSCRIPTEN__
	if (!capture_config.inputs) plat_input_vblank();
#else
	if (!capture_config.inputs && plat_input_vblank) plat_input_vblank();
#endif
	if (capture_config.audio_segments) {
		if (port_scene_audio_complete() &&
		    (!capture_config.checkpoints || port_scene_complete())) {
			port_scene_finish();
			if (clock_trace && fclose(clock_trace)) exit(1);
			puts("port: audio segments captured");
			exit(0);
		}
		return;
	}
	if (!marked || (capture_config.checkpoints ? !port_scene_complete() :
	    (uint32_t)(clock_vblanks() - mark_vblank) < capture_config.vblank))
		return;
	static uint32_t pixels[640 * 480];
	int width, height;
	if (!gpu_read_display(pixels, &width, &height)) {
		fprintf(stderr, "port: capture display is disabled\n");
		exit(1);
	}
	FILE *file = fopen(capture_config.capture, "wb");
	if (!file) {
		perror("port: capture");
		exit(1);
	}
	int failed = fprintf(file, "P6\n%d %d\n255\n", width, height) < 0;
	for (int i = 0; i < width * height && !failed; i++) {
		unsigned char rgb[] = {pixels[i] >> 16, pixels[i] >> 8, pixels[i]};
		failed = fwrite(rgb, 1, 3, file) != 3;
	}
	failed |= fclose(file) != 0;
	if (clock_trace)
		failed |= fclose(clock_trace) != 0;
	if (failed) {
		remove(capture_config.capture);
		fprintf(stderr, "port: cannot write capture or clock trace\n");
		exit(1);
	}
	port_scene_finish();
	printf("port: captured %dx%d at VBlank %u\n", width, height, clock_vblanks());
#ifdef __EMSCRIPTEN__
    MAIN_THREAD_EM_ASM({
        Module.capture = Array.from(FS.readFile(UTF8ToString($0)));
    }, capture_config.capture);
    web_finish(0);
#else
    exit(0);
#endif
}

/* AD-15 includes nonblocking VSync calls made inside PsyQ (set_alarm). */
void port_vsync_return(void)
{
	port_scene_origin();
#ifndef __EMSCRIPTEN__
	if (!have_frame0) {
        have_frame0 = 1; frame0_vblank = clock_vblanks();
        if (plat_native_wall_begin) plat_native_wall_begin();
    }
#endif
}

/* Only a blocking VSync presents a frame. */
void port_frame_done(void)
{
	plat_present(frames);
	frames++;
	if (frame_limit > 0 && frames >= frame_limit) {
		port_trace("frame %ld", frames - 1);
#ifdef __EMSCRIPTEN__
        MAIN_THREAD_EM_ASM({
            Module.print('port: reached frame ' + $0 + ' at cycle ' + $1);
        }, frames - 1, (double)clock_now());
        web_finish(capture_config.mark ? 1 : 0);
#else
		printf("port: reached frame %ld at cycle %llu\n", frames - 1,
		       (unsigned long long)clock_now());
        exit(capture_config.mark ? 1 : 0);
#endif
	}
}

void port_overlay_done(void)
{
	if (exit_on_overlay) {
		printf("port: overlay loaded at cycle %llu\n", (unsigned long long)clock_now());
		exit(capture_config.mark ? 1 : 0);
	}
}

/* No frame in WATCHDOG_SECONDS of virtual time: the game waits for something the port does not
 * do yet (a stub). Stop with the trace instead of spinning forever. */
#define WATCHDOG_SECONDS 30
static void watchdog(void *arg)
{
	(void)arg;
	if (frames == watchdog_frames) {
		fprintf(stderr, "port: no frame in %d virtual seconds (frame %ld, cycle %llu); see the "
		        "stub trace\n", WATCHDOG_SECONDS, frames, (unsigned long long)clock_now());
		exit(3);
	}
	watchdog_frames = frames;
	clock_schedule((uint64_t)WATCHDOG_SECONDS * CLOCK_HZ, watchdog, NULL);
}

static void *game_thread(void *arg)
{
	(void)arg;
	clock_advance(CLOCK_BOOT_SCAN_PHASE_CYCLES);
#ifndef __EMSCRIPTEN__
	if (plat_game_init) plat_game_init();
#endif
#ifdef __EMSCRIPTEN__
    if (web_prepare()) web_finish(1);
    volatile int stack_probe = 0;
    PORT_ASSERT(ps1_in_window((const void *)&stack_probe));
    /* Avoid racing main's stdio flush when it returns with a live pthread. */
    MAIN_THREAD_EM_ASM({
        Module.print('port: stack verified 0x' + ($0 >>> 0).toString(16));
    }, &stack_probe);
#endif
	if (capture_config.clock_trace) {
		clock_trace = fopen(capture_config.clock_trace, "w");
		if (!clock_trace) {
			perror("port: clock trace");
			exit(1);
		}
		clock_set_trace(trace_event);
	}
	clock_set_vblank_observer(capture_vblank);
	clock_schedule((uint64_t)WATCHDOG_SECONDS * CLOCK_HZ, watchdog, NULL);
	dw1_game_main();
	fprintf(stderr, "port: the game's main returned\n");
	exit(1);
}

int port_run_game(long frame_count, int until_overlay, const PortCapture *config)
{
	frame_limit = frame_count;
	exit_on_overlay = until_overlay;
	if (config)
		capture_config = *config;
	clock_reset();
	psyq_libspu_reset();
	port_scene_prepare(&capture_config);
	port_overlay_snapshot_all();
	/* Every object an OT can point to must be in the 16 MB window (AD-5); fatal in any build. */
	if (!ps1_in_window(game_stack) || !ps1_in_window(game_stack + GAME_STACK_SIZE - 1) ||
	    !ps1_in_window(Ps1_Ram) || !ps1_in_window(Ps1_Ram + PS1_RAM_SIZE - 1) ||
	    !ps1_in_window(_end - 1)) {
		fprintf(stderr, "port: the image (end %p) does not fit the OT window at %#x\n",
		        (void *)_end, PS1_WINDOW_BASE);
		return 1;
	}
	pthread_attr_t attr;
	pthread_t thread;
	if (pthread_attr_init(&attr) || pthread_attr_setstack(&attr, game_stack, GAME_STACK_SIZE) ||
#ifdef __EMSCRIPTEN__
        emscripten_pthread_attr_settransferredcanvases(&attr, "#canvas") ||
#endif
        pthread_create(&thread, &attr, game_thread, NULL)) {
		fprintf(stderr, "port: cannot start the game thread\n");
		return 1;
	}
    pthread_attr_destroy(&attr);
#ifdef __EMSCRIPTEN__
    pthread_detach(thread);
#else
    if (plat_main_loop) return plat_main_loop(thread);
    pthread_join(thread, NULL);
#endif
	return 0;
}
