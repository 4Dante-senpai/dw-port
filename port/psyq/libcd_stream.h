#ifndef PORT_LIBCD_STREAM_H
#define PORT_LIBCD_STREAM_H
#include <stdint.h>
/* Internal drive/ring seam. All calls execute on the virtual clock owner. */
int cd_stream_sector(const uint8_t *sector);
void cd_stream_eof(void);
void cd_stream_reset(void);
void cd_stream_stop(void);
int cd_stream_start(long mode);
#endif
