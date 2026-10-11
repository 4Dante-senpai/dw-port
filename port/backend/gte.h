#ifndef PORT_BACKEND_GTE_H
#define PORT_BACKEND_GTE_H

/* GTE (COP2) model. The port's inline_n.h maps the game's inline GTE macros to these calls. */

void port_gte_ldv0(const void *v0);
void port_gte_ldv3(const void *v0, const void *v1, const void *v2);
void port_gte_rtps(void);
void port_gte_rtpt(void);
void port_gte_stsxy(void *sxy);
void port_gte_stsxy3(void *sxy0, void *sxy1, void *sxy2);
void port_gte_stszotz(void *otz);
void port_gte_ldlzc(long value);
void port_gte_stlzc(void *lzc);
void port_gte_SetRotMatrix(const void *m);
void port_gte_SetTransMatrix(const void *m);

#endif
