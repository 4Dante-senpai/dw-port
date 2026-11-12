/* Build-independent observation; all calls run on the game thread. */
#ifndef PORT_PLAYTEST_H
#define PORT_PLAYTEST_H
#include "crt.h"
#include <stdint.h>
void port_playtest_prepare(const PortCapture *config);
void port_playtest_origin(void);
int port_playtest_started(void);
uint32_t port_playtest_vblank(void);
void port_playtest_pad(uint16_t mask);
void port_playtest_rand(int value);
void port_playtest_overlay(unsigned overlay);
void port_playtest_tick(void);
void port_playtest_finish(void);
const char *port_playtest_digest(void);
#endif
