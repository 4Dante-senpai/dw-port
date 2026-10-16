#ifndef PORT_GPU_H
#define PORT_GPU_H
#include <stdint.h>
#define GPU_VRAM_W 1024
#define GPU_VRAM_H 512
/* Bus words consumed and successful 16-bit VRAM writes. No virtual time in the backend. */
typedef struct { uint64_t words, pixels; } GpuWork;
void gpu_reset(int reset_display); /* GPU reset preserves VRAM. */
GpuWork gpu_gp0(const uint32_t *words, int count);
GpuWork gpu_fill(int x, int y, int w, int h, uint32_t rgb24);
GpuWork gpu_load(int x, int y, int w, int h, const uint16_t *src);
GpuWork gpu_store(int x, int y, int w, int h, uint16_t *dst);
GpuWork gpu_move(int sx, int sy, int dx, int dy, int w, int h);
const uint16_t *gpu_vram(void); /* Read-only, row-major 1024 x 512 halfwords. */
void gpu_set_display(int x, int y, int w, int h, int rgb24);
void gpu_set_display_enable(int on);
/* Decode display into caller storage (up to 640 x 480), 0x00RRGGBB; 0 when disabled. */
int gpu_read_display(uint32_t *out, int *w, int *h);
void gpu_draw_area_size(int *w, int *h);
#endif
