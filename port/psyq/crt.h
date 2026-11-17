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
	const char *audio_pcm;
	const char *resolved_inputs;
	const char *record_inputs;
	const char *digest;
	unsigned long digest_every;
	unsigned long vblank_limit; /* Stop N VBlanks after frame 0; independent of capture. */
	int state_fd, input_fd; /* Live playtest channel (native only); -1 = none. */
} PortCapture;

/* Starts the game on its own thread (static stack in the OT window) and waits. With
 * frame_count > 0 the process exits with status 0 when that many frames have ended; with
 * until_overlay, when the first overlay has been loaded. With a capture requested, either
 * limit instead exits with status 1 unless the capture has completed. NULL config disables capture. */
int port_run_game(long frame_count, int until_overlay, const PortCapture *config);
/* Ask the game thread to finish at its next VBlank (window close while recording). */
void port_request_stop(void);
/* Frames presented so far (VSync flips). */
long port_frame_count(void);
/* Scene captures run independently of wall time. */
int port_capture_active(void);
void port_scene_mark(const char *name);

/* Called on every VSync return, including nonblocking queries (AD-15). */
void port_vsync_return(void);

/* Called by VSync when a frame ends. */
void port_frame_done(void);
int port_pad_polled_take(void); /* PadRead called since the last call (libetc.c) */

/* Called by libcd when an overlay has been loaded. */
void port_overlay_done(void);

#endif
