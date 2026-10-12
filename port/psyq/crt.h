#ifndef PORT_CRT_H
#define PORT_CRT_H

/* Starts the game on its own thread (static stack in the OT window) and waits. With
 * frame_count > 0 the process exits with status 0 when that many frames have ended; with
 * until_overlay, when the first overlay has been loaded. */
int port_run_game(long frame_count, int until_overlay);

/* Called by VSync when a frame ends. */
void port_frame_done(void);

/* Called by libcd when an overlay has been loaded. */
void port_overlay_done(void);

#endif
