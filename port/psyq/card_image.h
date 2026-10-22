#ifndef PORT_CARD_IMAGE_H
#define PORT_CARD_IMAGE_H
#include <stddef.h>
#include <stdint.h>
#define CARD_IMAGE_SIZE 131072u
#define CARD_SECTOR_SIZE 128u
/* Host callbacks return zero on success. Missing images are supplied formatted by Host. */
typedef int (*CardCommit)(void *context, const uint8_t *image, size_t size);
void card_image_new(uint8_t image[CARD_IMAGE_SIZE]);
void card_image_attach(int port, const uint8_t *image, CardCommit commit, void *context);
uint8_t *card_image_data(int port);
int card_image_commit(int port);
void card_image_detach(int port);
#endif
