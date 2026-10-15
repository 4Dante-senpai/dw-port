#ifndef PORT_LIBGTE_H
#define PORT_LIBGTE_H

#include <stddef.h>

/* Load all four tables from the open CD Pack, or fail with a diagnostic. A failed load
 * clears every table. Static initialization supplies zero tables without a Pack. */
int libgte_load_tables(char *err, size_t errlen);
/* Raw little-endian bytes, also used to inject synthetic tables in L1 tests. Exact size
 * required. The returned view lets the Host hash precisely the bytes the functions use. */
int libgte_set_table(const char *name, const void *data, size_t size);
const void *libgte_table(const char *name, size_t *size);

#endif
