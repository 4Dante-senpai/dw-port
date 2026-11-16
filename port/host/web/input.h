#ifndef DW1_WEB_INPUT_H
#define DW1_WEB_INPUT_H
/* Live input on the web: sampled on the main thread, latched by the game thread each VBlank. */
void *web_input_address(void);
int web_input_replay(const char *path);
/* Replay VBlanks count from boot (resolved Scene inputs) instead of from frame 0 (recordings). */
void web_input_replay_absolute(void);
#endif
