#ifndef PORT_OVERLAY_H
#define PORT_OVERLAY_H

/*
 * Overlay data reset (AD-6). Every overlay TU is compiled with its global-storage variables in
 * the section dw1ovl_<ovl>; at boot the port copies each section, and a (re)load of the
 * overlay puts the copy back, as reloading the REL.BIN did on the console.
 */

/* Ids are the game's enum Overlay (include/dw/utils.h): 1 = BTL_REL ... 16 = EAB_REL. */
#define PORT_OVERLAY_COUNT 17

void port_overlay_snapshot_all(void);
void port_overlay_loaded(int lib);

/* Size in bytes of the overlay's data section (0 if it has none). */
unsigned port_overlay_size(int lib);

#endif
