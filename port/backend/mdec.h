#ifndef DW1_MDEC_H
#define DW1_MDEC_H
#include <stddef.h>
#include <stdint.h>
/* Reset decoding/FIFO state, preserving the uploaded tables. */
void mdec_reset(void);
/* Complete commands 1/2/3. Decode parameters remain borrowed until output is read. */
int mdec_command(uint32_t command, const void *parameters, size_t words);
/* DMA order: 16x16 macroblocks, raster order within each; partial reads persist. */
void mdec_out(void *out, size_t words);
#endif
