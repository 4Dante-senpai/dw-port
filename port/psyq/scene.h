#ifndef PORT_SCENE_H
#define PORT_SCENE_H
#include <stdint.h>
#include "crt.h"
void port_scene_audio_output_init(void);
void port_scene_resolved_input(uint16_t mask);
void port_scene_prepare(const PortCapture *config);
void port_scene_origin(void);
void port_scene_start(void);
void port_scene_tick(void);
void port_scene_finish(void);
int port_scene_complete(void);
int port_scene_audio_complete(void);
void port_scene_enter(void *function);
/* Optional movie timing observation; never advances the virtual clock. */
void port_scene_movie_timing(const char *event, unsigned value);
#endif
