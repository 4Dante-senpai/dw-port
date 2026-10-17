#ifndef DW1_IO_MAILBOX_H
#define DW1_IO_MAILBOX_H
#include <stdint.h>
void *io_mailbox_address(void);
uint64_t io_size(void);
int io_read(uint64_t offset, void *out, uint32_t length);
int web_prepare(void);
void web_finish(int code) __attribute__((noreturn));
#endif
