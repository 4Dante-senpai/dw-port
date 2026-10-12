/*
 * libgs data the game references directly (sizes from config/us/symbols.txt). The identity
 * matrix has its fixed value; the others are filled by libgs at run time (story 2.13).
 */
/* MATRIX { short m[3][3]; long t[3]; }: ONE (4096) on the diagonal, no translation. */
struct { short m[3][3]; long t[3]; } GsIDMATRIX = { { { 4096, 0, 0 }, { 0, 4096, 0 }, { 0, 0, 4096 } }, { 0, 0, 0 } };
_Static_assert(sizeof GsIDMATRIX == 0x20, "MATRIX is 0x20 bytes");
unsigned char GsWSMATRIX[0x20];
unsigned char GsLIGHTWSMATRIX[0x20];
unsigned char GsFCALL4[0x130];
int StCdIntrFlag; /* libcd streaming */
