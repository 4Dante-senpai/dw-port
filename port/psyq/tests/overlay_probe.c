/* Linked with every game object (unresolved symbols ignored, no game code runs; the game's own
 * main is bypassed with -Wl,--wrap=main): prints the size of each overlay data section and
 * checks the image layout and the translated addresses in static game data. */
#include <stdio.h>

#include "../overlay.h"
#include "../ps1mem.h"
#include "port_game.h"

extern char _end[];

#include "dw1_overlay_starts.h" /* <ovl>_START, also compiled into src/main/utils2.c */

#define shop_START ((char *)PS1_RAM_ADDR(0x80080800u)) /* hand-made in utils2.c (USA) */

/* Game data initialized with translated PS1 addresses (story 2.4). The types are the ABI
 * equivalents of the game's own declarations (u_long *, uint8_t *, int32_t). */
extern void *REL_BIN_OFFSETS[16];
extern char *BOSS_EFE_TMD;
extern unsigned long *ENDI_FADE_CLUT_BUFFER;
extern unsigned char *ARRAY_SECTION_START;
extern int MAIN_D_801349E4;

static int addresses_ok(void)
{
	/* Same order as REL_BIN_OFFSETS in src/main/utils2.c (enum Overlay - 1). */
	char *const starts[16] = {
		btl_START, std_START, fish_START, evl_START, kar_START, vs_START, mov_START, doo2_START,
		dooa_START, trn_START, shop_START, dget_START, trn2_START, murd_START, endi_START,
		eab_START,
	};
	int ok = REL_BIN_OFFSETS[0] == Ps1_Ram + (0x80052AE0 & 0x1FFFFF) && /* btl, from btl.yaml */
	         (void *)ENDI_FADE_CLUT_BUFFER == Ps1_Ram + 0x10000 &&      /* GENERAL_BUFFER */
	         (void *)BOSS_EFE_TMD == Ps1_Ram + 0x58000 &&               /* BOSS_EFE_TMD_BUFFER */
	         ARRAY_SECTION_START == Ps1_Ram + 0x88800 &&                /* TEXTURE_BUFFER */
	         MAIN_D_801349E4 == (int)(Ps1_Ram + 0x20000);               /* EVL_MMD_BUFFER */
	for (int i = 0; i < 16; i++)
		ok = ok && REL_BIN_OFFSETS[i] == starts[i] && ps1_in_window(REL_BIN_OFFSETS[i]);
	return ok;
}

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
	int addr_ok = addresses_ok();
	printf("overlays_with_data=%d expected=%d image_end=%p in_window=%d addresses=%s\n",
	       with_data, DW1_OVERLAY_UNITS, (void *)_end, image_ok, addr_ok ? "OK" : "FAIL");
	return !(with_data == DW1_OVERLAY_UNITS && image_ok && addr_ok);
}
