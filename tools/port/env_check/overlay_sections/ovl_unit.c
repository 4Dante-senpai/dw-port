/* Stand-in for an overlay source file from src/<ovl>/: globals, statics, a static local. */
int btl_x = 5;
int btl_y[4] = {1, 2, 3, 4};
int btl_z;
static int hidden = 3;
static char hbss[300];

int *ovl_hidden(void) { return &hidden; }
char *ovl_hbss(void) { return hbss; }
int ovl_counter(void) { static int calls = 10; return calls++; }
