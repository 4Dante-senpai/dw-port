/* Sparse synthetic sound tables. Full retail data is tested only with the user disc. */
#include "../sound_tables.h"
#include <math.h>
#include <string.h>
static void sound_fixture(void)
{
    uint8_t starts[40] = {0}, presets[688] = {0}, pitch[400] = {0};
    for (unsigned i = 0; i < 10; i++) {
        /* Synthetic work areas; retain only the isolated API expectations. */
        uint16_t address = 0xf000 + i * 0x100;
        if (i == 0) address = 0xfffe;
        if (i == 1) address = 0xfb28;
        if (i == 7) address = 0xcff8;
        starts[i * 4] = address; starts[i * 4 + 1] = address >> 8;
    }
    /* Only registers whose values the API tests inspect are supplied. */
    const struct { unsigned mode, index; uint16_t value; } registers[] = {
        {1, 0, 0x7d}, {1, 2, 0x6d80}, {1, 7, 0xba80},
        {7, 0, 1}, {7, 1, 1}, {7, 7, 0x8100}, {8, 0, 1}, {8, 1, 1}
    };
    for (unsigned i = 0; i < sizeof(registers) / sizeof(*registers); i++) {
        unsigned offset = registers[i].mode * 68 + 4 + registers[i].index * 2;
        presets[offset] = registers[i].value; presets[offset + 1] = registers[i].value >> 8;
    }
    for (unsigned i = 0; i <= 192; i++) {
        uint16_t value = (uint16_t)(4096 * pow(2.0, i / 192.0));
        pitch[i * 2] = value; pitch[i * 2 + 1] = value >> 8;
    }
    CHECK(sound_set_table("_spu_rev_startaddr", starts, sizeof(starts)) == 0);
    CHECK(sound_set_table("_spu_rev_param", presets, sizeof(presets)) == 0);
    CHECK(sound_set_table("_svm_ptable", pitch, sizeof(pitch)) == 0);
}
