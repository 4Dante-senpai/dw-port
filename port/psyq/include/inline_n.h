#ifndef PORT_INLINE_N_H
#define PORT_INLINE_N_H

/*
 * Replaces include/gcc/inline_n.h: the inline GTE macros the game uses become calls into the
 * GTE model of the Backend. Only the macros used by src/ are defined, so a new use fails to
 * compile instead of silently doing nothing.
 */

#include "../../backend/gte.h"

#define gte_ldv0(r0) port_gte_ldv0(r0)
#define gte_ldv3(r0, r1, r2) port_gte_ldv3(r0, r1, r2)
#define gte_rtps() port_gte_rtps()
#define gte_rtpt() port_gte_rtpt()
#define gte_stsxy(r0) port_gte_stsxy(r0)
#define gte_stsxy3(r0, r1, r2) port_gte_stsxy3(r0, r1, r2)
#define gte_stszotz(r0) port_gte_stszotz(r0)
#define gte_ldlzc(r0) port_gte_ldlzc(r0)
#define gte_stlzc(r0) port_gte_stlzc(r0)
#define gte_SetRotMatrix(r0) port_gte_SetRotMatrix(r0)
#define gte_SetTransMatrix(r0) port_gte_SetTransMatrix(r0)

#endif
