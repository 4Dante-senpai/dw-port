#ifndef PORT_GAME_MEMORY_H
#define PORT_GAME_MEMORY_H
#ifdef DW1_ABI_AUDIT
#include_next <memory.h>
#else
/* mw_lib41 declares bzero with a pointer result; the host implementation returns void. */
#define bzero dw1_unused_bzero_declaration
#include_next <memory.h>
#undef bzero
void bzero(void *buffer, unsigned int size);
#endif
#endif
