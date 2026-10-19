#ifndef PORT_CRT_H
#define PORT_CRT_H

typedef struct {
	const char *mark;
	unsigned long vblank;
	const char *capture;
	const char *clock_trace;
	const char *inputs;
	const char *inputs_from;
	const char *state_vars;
	const char *state_trace;
	const char *checkpoints;
	const char *audio_segments;
} PortCapture;

/* Starts the game on its own thread (static stack in the OT window) and waits. With
 * frame_count > 0 the process exits with status 0 when that many frames have ended; with
 * until_overlay, when the first overlay has been loaded. With a capture requested, either
 * limit instead exits with status 1 unless the capture has completed. NULL config disables capture. */
int port_run_game(long frame_count, int until_overlay, const PortCapture *config);
void port_scene_mark(const char *name);

/* Called by VSync when a frame ends. */
void port_frame_done(void);

/* Called by libcd when an overlay has been loaded. */
void port_overlay_done(void);

#endif
