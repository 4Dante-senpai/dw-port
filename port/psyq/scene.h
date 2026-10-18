#ifndef PORT_SCENE_H
#define PORT_SCENE_H
#include "crt.h"
void port_scene_prepare(const PortCapture *config);
void port_scene_origin(void);
void port_scene_start(void);
void port_scene_tick(void);
void port_scene_finish(void);
int port_scene_complete(void);
void port_scene_enter(void *function);
#endif
