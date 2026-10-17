#ifndef PORT_PLAT_H
#define PORT_PLAT_H
#include <stdint.h>
#ifdef __EMSCRIPTEN__
void plat_present(long frame);
#else
static inline void plat_present(long frame) { (void)frame; }
#endif
#endif
