#ifndef DW1_PORT_LIBPRESS_H
#define DW1_PORT_LIBPRESS_H
#include <stddef.h>
int libpress_load_tables(char *error, size_t length);
int libpress_set_table(const char *name, const void *data, size_t size);
const void *libpress_table(const char *name, size_t *size);
#endif
