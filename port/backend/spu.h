/* Adapted from Wyrelade/DW2-PC backend/psxspu.h (CC0 1.0). */
#ifndef PORT_BACKEND_SPU_H
#define PORT_BACKEND_SPU_H
#include <stdint.h>
#include <stdio.h>
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
/* Optional capture of one checkpoint-relative segment. Borrowed streams must be
 * opened outside the repository; WAV must be seekable. No ownership is taken.
 * Trace rows: VBlank sample register-offset value (decimal, decimal, hex, hex).
 * begin drains earlier PCM; end drains the segment and patches the WAV header.
 * Returns -1 on invalid state or I/O failure. Existing output callbacks still run. */
int spu_capture_begin(FILE *trace, FILE *wav);
/* Arm at a checkpoint, then rebase to the first new sample-address/KON bundle.
 * WAV must be readable/seekable (w+b). Fails closed if no bundle is confirmed. */
void spu_capture_note_anchor(void);
int spu_capture_origin(uint64_t *cycle, uint32_t *vblank);
int spu_capture_end(void);
#endif
