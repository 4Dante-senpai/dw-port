#include <stdio.h>
#include <string.h>

extern char __start_dw1ovl_btl[], __stop_dw1ovl_btl[];
extern int btl_x, btl_z, btl_y[4];
int *ovl_hidden(void);
char *ovl_hbss(void);
int ovl_counter(void);

int main_global = 1; /* outside the overlay: must survive the reset */
static char snapshot[4096];

__attribute__((noinline)) static void ovl_snapshot(void)
{
    memcpy(snapshot, __start_dw1ovl_btl, __stop_dw1ovl_btl - __start_dw1ovl_btl);
}

__attribute__((noinline)) static void ovl_restore(void)
{
    memcpy(__start_dw1ovl_btl, snapshot, __stop_dw1ovl_btl - __start_dw1ovl_btl);
    __asm__ volatile("" ::: "memory");
}

int main(void)
{
    ovl_snapshot();
    btl_x = 99; btl_z = 7; btl_y[3] = 0; *ovl_hidden() = 42; ovl_hbss()[1] = 9;
    ovl_counter(); ovl_counter();
    main_global = 12;
    ovl_restore();
    int ok = btl_x == 5 && btl_z == 0 && btl_y[3] == 4 && *ovl_hidden() == 3 && ovl_hbss()[1] == 0 &&
             ovl_counter() == 10 && main_global == 12;
    printf("section=%ld bytes ptr=%zu %s\n", (long)(__stop_dw1ovl_btl - __start_dw1ovl_btl),
           sizeof(void *), ok ? "OK" : "FAIL");
    return !ok;
}
