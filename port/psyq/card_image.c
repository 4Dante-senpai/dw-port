#include "card_image.h"
#include <string.h>
static struct {
	uint8_t data[CARD_IMAGE_SIZE];
	int present;
	CardCommit commit;
	void *context;
} cards[2];
/* psx-spx Memory Card Data Format. Redux blank image verified without booting a disc:
 * SHA256 78b6d4ac9ab4d23caf7e5f04f83539bf5d994cccfb0a709d14ac53d05c8e21ef. */
void card_image_new(uint8_t image[CARD_IMAGE_SIZE])
{
	memset(image, 0, CARD_IMAGE_SIZE);
	image[0] = 'M';
	image[1] = 'C';
	image[127] = 'M' ^ 'C';
	for (int i = 1; i < 16; i++) {
		uint8_t *s = image + i * 128;
		s[0] = s[127] = 0xa0;
		s[8] = s[9] = 0xff;
	}
	for (int i = 16; i < 36; i++) {
		uint8_t *s = image + i * 128;
		memset(s, 0xff, 4);
		s[8] = s[9] = 0xff;
	}
}
void card_image_attach(int port, const uint8_t *image, CardCommit commit, void *context)
{
	if (port < 0 || port > 1) {
		return;
	}
#ifdef __EMSCRIPTEN__
	(void)image;
	(void)commit;
	(void)context;
	cards[port].present = 0; /* Native persistence only; web support is Epic 4. */
#else
	if (image) {
		memcpy(cards[port].data, image, CARD_IMAGE_SIZE);
	}
	cards[port].present = image != NULL;
	cards[port].commit = commit;
	cards[port].context = context;
#endif
}
uint8_t *card_image_data(int port)
{
	return port >= 0 && port < 2 && cards[port].present ? cards[port].data : NULL;
}
int card_image_commit(int port)
{
	if (!card_image_data(port)) {
		return -1;
	}
	return cards[port].commit ? cards[port].commit(cards[port].context, cards[port].data, CARD_IMAGE_SIZE) : 0;
}
void card_image_detach(int port)
{
	card_image_attach(port, NULL, NULL, NULL);
}
