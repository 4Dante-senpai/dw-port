#ifndef DW1_SOUND_TABLES_H
#define DW1_SOUND_TABLES_H
#include <stddef.h>
#include <stdint.h>
int sound_load_tables(char *err, size_t errlen);
const void *sound_table(const char *name, size_t *size);
/* Synthetic fixtures may supply table data without a user disc. */
int sound_set_table(const char *name, const void *data, size_t size);
uint16_t sound_reverb_start(unsigned mode);
uint16_t sound_reverb_param(unsigned mode, unsigned index);
uint16_t sound_pitch(unsigned index);
#define SOUND_PITCH_COUNT 200
#endif
