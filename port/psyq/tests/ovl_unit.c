/* Stand-in for an overlay source file from src/<ovl>/: globals, statics, a static local. */
int btl_x = 5;
int btl_y[4] = {1, 2, 3, 4};
int btl_z;
static int hidden = 3;
static char hbss[300];

int *ovl_hidden(void) { return &hidden; }
char *ovl_hbss(void) { return hbss; }
int ovl_counter(void) { static int calls = 10; return calls++; }

/* A const table, as in src/endi/endi.c (endi_functions). */
static void *const ovl_table[] = { ovl_hidden, ovl_hbss };
void *ovl_table_entry(int i) { return ovl_table[i]; }

/* A main global seen from the overlay: the extern declaration must not move it. */
extern int main_global;
int *ovl_main_global(void) { return &main_global; }
