#ifndef PORT_PS1MEM_H
#define PORT_PS1MEM_H

/* PS1 memory model of the port (AD-3, AD-4). Both builds are 32-bit like the PS1. */

_Static_assert(sizeof(void *) == 4, "the port requires 32-bit pointers (native is built with -m32)");

#endif
