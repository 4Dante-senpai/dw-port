/* Synthetic loader errors, atomic cleanup and table API validation. */
#include "../sound_tables.h"
#include "../../backend/cdrom.h"
#include "../port.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); exit(1); } } while (0)
int main(void)
{
    const char *names[] = {"_spu_rev_startaddr", "_spu_rev_param", "_svm_ptable"};
    char error[256];
    size_t size = 1;
    CHECK(sound_table("unknown", &size) == NULL && size == 0);
    CHECK(sound_set_table("unknown", error, sizeof(error)) == -1);
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < 3; i++) {
            CHECK(sound_table(names[i], &size) != NULL);
            unsigned char *data = malloc(size);
            CHECK(data != NULL);
            memset(data, 0xa5, size);
            CHECK(sound_set_table(names[i], NULL, size) == -1);
            CHECK(sound_set_table(names[i], data, size - 1) == -1);
            CHECK(sound_set_table(names[i], data, size) == 0);
            free(data);
        }
        if (pass) CHECK(cdrom_open("pack_t1.pack", NULL, error, sizeof(error)) == 0);
        else cdrom_close();
        CHECK(sound_load_tables(error, sizeof(error)) == -1);
        CHECK(strstr(error, "missing or short") != NULL);
        for (unsigned i = 0; i < 3; i++) {
            const unsigned char *data = sound_table(names[i], &size);
            for (size_t j = 0; j < size; j++) CHECK(data[j] == 0);
        }
    }
    cdrom_close();
    puts("sound_tables: missing/short EXE, invalid input and cleared tables OK");
    return 0;
}
