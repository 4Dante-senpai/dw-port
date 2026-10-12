#ifndef PORT_PORT_H
#define PORT_PORT_H

/* Common definitions of the port code. */

#include <stdio.h>
#include <stdlib.h>

/* Invariant check: aborts in debug builds, logs in release builds. */
#ifdef NDEBUG
#define PORT_ASSERT(cond)                                                                  \
	((cond) ? (void)0                                                                      \
	        : (void)fprintf(stderr, "PORT_ASSERT %s:%d: %s\n", __FILE__, __LINE__, #cond))
#else
#define PORT_ASSERT(cond)                                                                  \
	((cond) ? (void)0                                                                      \
	        : (fprintf(stderr, "PORT_ASSERT %s:%d: %s\n", __FILE__, __LINE__, #cond), abort()))
#endif

/* One line "<cycle> <text>" in the trace file $DW1_TRACE (default port_trace.log). */
void port_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
