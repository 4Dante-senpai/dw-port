/* Adapted from Wyrelade/DW2-PC backend/psxspu.h (CC0 1.0). */
#ifndef PORT_BACKEND_SPU_H
#define PORT_BACKEND_SPU_H
#include <stdint.h>
#define SPU_RAM_SIZE 0x80000u
extern uint8_t spu_ram[SPU_RAM_SIZE];
/* Offsets from 0x1f801c00, even and below 0x400. All accesses synchronize first. */
void spu_reset(void);
void spu_sync(void);
void spu_write16(uint32_t offset, uint16_t value);
uint16_t spu_read16(uint32_t offset);
void spu_dma_write(const uint32_t *src, uint32_t words);
void spu_dma_read(uint32_t *dst, uint32_t words);
int spu_transfer_completed(void);
uint64_t spu_samples(void);
uint32_t spu_keys(void);
/* NULL input supplies silence; NULL output discards samples. Callbacks must not reenter SPU. */
typedef void (*SpuCdInput)(int16_t *left, int16_t *right, void *arg);
typedef void (*SpuOutput)(const int16_t *pcm, unsigned frames, void *arg);
void spu_set_cd_input(SpuCdInput fn, void *arg);
void spu_set_output(SpuOutput fn, void *arg);
#endif
