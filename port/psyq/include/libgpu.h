#ifndef PORT_GAME_LIBGPU_H
#define PORT_GAME_LIBGPU_H
#include_next <libgpu.h>
#include <stdint.h>
uint32_t port_link_primitive(const void *p);

/* PsyQ's lowercase packet macro has no call cost. Native addresses must use the
 * same OT window offsets as AddPrim, rather than truncating host pointers. */
#undef addPrim
static inline void port_add_prim(void *ot, void *primitive)
{
    uint32_t *o = ot, *p = primitive;
    *p = (*p & 0xff000000u) | (*o & 0x00ffffffu);
    *o = (*o & 0xff000000u) | port_link_primitive(primitive);
}
#define addPrim(ot, p) port_add_prim((ot), (p))
#endif
