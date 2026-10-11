/* L1: an overlay reload restores every global-storage variable of the overlay (AD-6). */
#include <stdio.h>

#include "../overlay.h"

extern int btl_x, btl_z, btl_y[4];
int *ovl_hidden(void);
char *ovl_hbss(void);
int ovl_counter(void);
int *ovl_main_global(void);
void *ovl_table_entry(int i);

extern int std_value, btl_second;

int main_global = 1; /* outside the overlay: must survive the reset */

int main(void)
{
	port_overlay_snapshot_all();
	btl_x = 99;
	btl_z = 7;
	btl_y[3] = 0;
	*ovl_hidden() = 42;
	ovl_hbss()[1] = 9;
	ovl_counter();
	ovl_counter();
	main_global = 12;
	btl_second = 0;
	std_value = 0;

	port_overlay_loaded(11); /* SHOP_REL: no section, nothing happens */
	int ok = btl_x == 99;
	port_overlay_loaded(1); /* BTL_REL */
	ok = ok && btl_x == 5 && btl_z == 0 && btl_y[3] == 4 && *ovl_hidden() == 3 &&
	     ovl_hbss()[1] == 0 && ovl_counter() == 10 && main_global == 12 &&
	     btl_second == 8 && std_value == 0 && ovl_main_global() == &main_global && ovl_table_entry(0) == (void *)ovl_hidden && port_overlay_size(1) > 300 && port_overlay_size(11) == 0;
	port_overlay_loaded(2); /* STD_REL */
	ok = ok && std_value == 21 && btl_x == 5;
	printf("test_overlay: btl section %u bytes %s\n", port_overlay_size(1), ok ? "OK" : "FAIL");
	return !ok;
}
