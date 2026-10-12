#include "overlay.h"

#include <stdlib.h>
#include <string.h>

#include "port.h"

/* weak: an overlay without global data has no section (SHOP has no segment in USA). */
#define OVL(name)                                                                          \
	extern char __start_dw1ovl_##name[] __attribute__((weak));                             \
	extern char __stop_dw1ovl_##name[] __attribute__((weak));
OVL(btl) OVL(std) OVL(fish) OVL(evl) OVL(kar) OVL(vs) OVL(mov) OVL(doo2) OVL(dooa)
OVL(trn) OVL(shop) OVL(dget) OVL(trn2) OVL(murd) OVL(endi) OVL(eab)
#undef OVL

typedef struct {
	char *start;
	char *stop;
	char *copy;
} OverlayData;

#define OVL(id, name) [id] = { __start_dw1ovl_##name, __stop_dw1ovl_##name, NULL },
static OverlayData overlays[PORT_OVERLAY_COUNT] = {
	OVL(1, btl) OVL(2, std) OVL(3, fish) OVL(4, evl) OVL(5, kar) OVL(6, vs) OVL(7, mov)
	OVL(8, doo2) OVL(9, dooa) OVL(10, trn) OVL(11, shop) OVL(12, dget) OVL(13, trn2)
	OVL(14, murd) OVL(15, endi) OVL(16, eab)
};
#undef OVL

/* REL_BIN_FILES of src/main/utils2.c, in enum Overlay order. */
static const char *const files[PORT_OVERLAY_COUNT] = {
	NULL, "BTL_REL.BIN", "STD_REL.BIN", "FISH_REL.BIN", "EVL_REL.BIN", "KAR_REL.BIN",
	"VS_REL.BIN", "MOV_REL.BIN", "DOO2_REL.BIN", "DOOA_REL.BIN", "TRN_REL.BIN", "SHOP_REL.BIN",
	"DGET_REL.BIN", "TRN2_REL.BIN", "MURD_REL.BIN", "ENDI_REL.BIN", "EAB_REL.BIN"
};

static int snapshot_taken;

int port_overlay_id(const char *file)
{
	for (int i = 1; i < PORT_OVERLAY_COUNT; i++) {
		if (!strcmp(file, files[i]))
			return i;
	}
	return 0;
}

void port_overlay_snapshot_all(void)
{
	snapshot_taken = 1;
	for (int i = 1; i < PORT_OVERLAY_COUNT; i++) {
		OverlayData *o = &overlays[i];
		size_t size = (size_t)(o->stop - o->start);
		if (o->start == NULL || size == 0 || o->copy != NULL)
			continue;
		o->copy = malloc(size);
		PORT_ASSERT(o->copy != NULL);
		memcpy(o->copy, o->start, size);
	}
}

void port_overlay_loaded(int lib)
{
	PORT_ASSERT(snapshot_taken);
	PORT_ASSERT(lib > 0 && lib < PORT_OVERLAY_COUNT);
	if (lib <= 0 || lib >= PORT_OVERLAY_COUNT)
		return; /* release build: PORT_ASSERT only logged */
	OverlayData *o = &overlays[lib];
	if (o->copy == NULL)
		return; /* the overlay has no global data */
	memcpy(o->start, o->copy, (size_t)(o->stop - o->start));
}

unsigned port_overlay_size(int lib)
{
	PORT_ASSERT(lib > 0 && lib < PORT_OVERLAY_COUNT);
	if (lib <= 0 || lib >= PORT_OVERLAY_COUNT)
		return 0;
	return (unsigned)(overlays[lib].stop - overlays[lib].start);
}
