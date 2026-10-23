/* Exercise the real headless CLI and Host with a disc-free game entry. */
#define main card_cli_main
#define DW1_MANIFEST_SHA256 "0000000000000000000000000000000000000000000000000000000000000000"
#include "../../host/native_cli.c"
#include "../../host/headless/main.c"
#undef main
#include "../psyq.h"
#include "../card_image.h"
#include "../clock.h"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>

int port_run_game(long frames, int until_overlay, const PortCapture *config)
{
	(void)frames;
	(void)until_overlay;
	(void)config;
	assert(card_image_data(0) && card_image_data(1));
	assert(card_image_data(0)[8192] == 0x11);
	assert(card_image_data(1)[8192] == 0x22);
	clock_reset();
	ResetCallback();
	MemCardInit(1);
	MemCardStart();
	unsigned long command, result;
	assert(MemCardExist(16));
	assert(MemCardSync(0, &command, &result) == 1 && command == 1 && result == 3);
	assert(MemCardCreateFile(16, "SECOND", 1) == 0);
	long data[32];
	memset(data, 0x5a, sizeof(data));
	assert(MemCardWriteFile(16, "SECOND", data, 0, sizeof(data)));
	assert(MemCardSync(0, &command, &result) == 1 && command == 4 && result == 0);
	MemCardStop();
	return 0;
}

static void image_file(const char *path, uint8_t *image, const char *mode)
{
	FILE *stream = fopen(path, mode);
	assert(stream);
	assert((mode[0] == 'w' ? fwrite(image, 1, CARD_IMAGE_SIZE, stream)
	                       : fread(image, 1, CARD_IMAGE_SIZE, stream)) == CARD_IMAGE_SIZE);
	assert(fclose(stream) == 0);
}

int main(int argc, char **argv)
{
	if (argc > 1) return card_cli_main(argc, argv);
	char directory[] = "/tmp/dw1-card-cli-XXXXXX";
	assert(mkdtemp(directory));
	char first[128], second[128];
	snprintf(first, sizeof(first), "%s/first.mcd", directory);
	snprintf(second, sizeof(second), "%s/second.mcd", directory);
	static uint8_t original[CARD_IMAGE_SIZE], actual[CARD_IMAGE_SIZE];
	card_image_new(original);
	original[8192] = 0x11;
	image_file(first, original, "wb");
	memcpy(actual, original, sizeof(actual));
	actual[8192] = 0x22;
	image_file(second, actual, "wb");
	pid_t child = fork();
	assert(child >= 0);
	if (!child) {
		execl(argv[0], argv[0], "--card1", first, "--card2", second, "--frames", "1", (char *)NULL);
		_exit(127);
	}
	int status;
	assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	image_file(first, actual, "rb");
	assert(!memcmp(actual, original, sizeof(actual)));
	image_file(second, actual, "rb");
	assert(actual[128] == 0x51 && !strcmp((char *)actual + 138, "SECOND"));
	for (unsigned i = 8192; i < 8192 + 128; i++) assert(actual[i] == 0x5a);
	assert(unlink(first) == 0 && unlink(second) == 0 && rmdir(directory) == 0);
	puts("card_cli: --card2 present; writes persist only to the second path OK");
	return 0;
}
