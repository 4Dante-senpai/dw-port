/* PsyQ sound tables loaded from the user Pack executable (AD-12). */
#include "sound_tables.h"
#include "../backend/cdrom.h"
#include "port.h"
#include "dw1_sound_tables.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t _spu_rev_startaddr[DW1_SOUND__SPU_REV_STARTADDR_SIZE];
static uint8_t _spu_rev_param[DW1_SOUND__SPU_REV_PARAM_SIZE];
static uint8_t _svm_ptable[DW1_SOUND__SVM_PTABLE_SIZE];
static const struct Table {
    const char *name;
    uint8_t *data;
    uint32_t offset, size;
} tables[] = {
    {"_spu_rev_startaddr", _spu_rev_startaddr, DW1_SOUND__SPU_REV_STARTADDR_OFFSET, sizeof(_spu_rev_startaddr)},
    {"_spu_rev_param", _spu_rev_param, DW1_SOUND__SPU_REV_PARAM_OFFSET, sizeof(_spu_rev_param)},
    {"_svm_ptable", _svm_ptable, DW1_SOUND__SVM_PTABLE_OFFSET, sizeof(_svm_ptable)},
};
const void *sound_table(const char *name, size_t *size)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!strcmp(name, tables[i].name)) {
            *size = tables[i].size;
            return tables[i].data;
        }
    }
    *size = 0;
    return NULL;
}

int sound_set_table(const char *name, const void *data, size_t size)
{
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        if (!strcmp(name, tables[i].name) && size == tables[i].size && data) {
            memcpy(tables[i].data, data, size);
            return 0;
        }
    }
    return -1;
}

int sound_load_tables(char *err, size_t errlen)
{
    const PackEntry *exe = cdrom_find_path(PACK_EXE_PATH);
    for (unsigned i = 0; i < sizeof(tables) / sizeof(tables[0]); i++) {
        const struct Table *t = &tables[i];
        if (!exe || cdrom_read_file(exe, t->offset, t->data, t->size) != 0) {
            snprintf(err, errlen, "sound: cannot load %s from the Pack executable (missing or short)", t->name);
            for (unsigned j = 0; j < sizeof(tables) / sizeof(tables[0]); j++)
                memset(tables[j].data, 0, tables[j].size);
            return -1;
        }
    }
    return 0;
}

_Static_assert(DW1_SOUND__SPU_REV_STARTADDR_SIZE == 40, "reverb address region");
_Static_assert(DW1_SOUND__SPU_REV_PARAM_SIZE == 688, "reverb preset region");
_Static_assert(DW1_SOUND__SVM_PTABLE_SIZE == 400, "pitch region");
static uint16_t half(const uint8_t *data, unsigned offset)
{
    return data[offset] | (uint16_t)data[offset + 1] << 8;
}
uint16_t sound_reverb_start(unsigned mode) { return half(_spu_rev_startaddr, mode * 4); }
/* Retail preset records have a 4-byte mask followed by 32 halfword registers. */
uint16_t sound_reverb_param(unsigned mode, unsigned index)
{
    return half(_spu_rev_param, mode * 68 + 4 + index * 2);
}
uint16_t sound_pitch(unsigned index) { return half(_svm_ptable, index * 2); }
