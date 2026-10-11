/* Linked with every game object (unresolved symbols ignored, no game code runs; the game's own
 * main is bypassed with -Wl,--wrap=main): prints the size of each overlay data section. */
#include <stdio.h>

#include "../overlay.h"
#include "../ps1mem.h"

extern char _end[];

static const char *const names[PORT_OVERLAY_COUNT] = {
	"", "btl", "std", "fish", "evl", "kar", "vs", "mov", "doo2", "dooa", "trn", "shop",
	"dget", "trn2", "murd", "endi", "eab",
};

int __wrap_main(void);

int __wrap_main(void)
{
	int with_data = 0;
	for (int i = 1; i < PORT_OVERLAY_COUNT; i++) {
		unsigned size = port_overlay_size(i);
		printf("%s\t%u\n", names[i], size);
		with_data += size != 0;
	}
	port_overlay_snapshot_all();
	/* The whole image, with every game object, fits in the OT window (AD-5). */
	int image_ok = ps1_in_window(_end - 1);
	printf("overlays_with_data=%d expected=%d image_end=%p in_window=%d\n", with_data,
	       DW1_OVERLAY_UNITS, (void *)_end, image_ok);
	return !(with_data == DW1_OVERLAY_UNITS && image_ok);
}
