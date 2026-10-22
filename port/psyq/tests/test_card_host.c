#include "../../host/card.h"
#include "../card_image.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <glob.h>
static void disk(const char *path, uint8_t *data)
{
	FILE *f = fopen(path, "rb");
	assert(f);
	assert(fread(data, 1, CARD_IMAGE_SIZE, f) == CARD_IMAGE_SIZE);
	assert(fgetc(f) == EOF);
	fclose(f);
}
int main(void)
{
	char dir[] = "/tmp/dw1-card-l1-XXXXXX", path[256];
	assert(mkdtemp(dir));
	snprintf(path, sizeof(path), "%s/card.mcd", dir);
	assert(host_card_open(0, path) == 0);
	uint8_t old[CARD_IMAGE_SIZE], check[CARD_IMAGE_SIZE];
	card_image_new(old);
	disk(path, check);
	assert(!memcmp(old, check, sizeof(old)));
	uint8_t *image = card_image_data(0);
	memset(image + 8192, 0x36, 8192);
	assert(card_image_commit(0) == 0);
	memcpy(old, image, sizeof(old));
	host_card_close(0);
	const char *points[] = { "during-temp", "before-rename" };
	for (int i = 0; i < 2; i++) {
		pid_t pid = fork();
		assert(pid >= 0);
		if (!pid) {
			assert(!host_card_open(0, path));
			memset(card_image_data(0) + 8192, 0xa5, 8192);
			setenv("DW1_CARD_TEST_KILL", points[i], 1);
			card_image_commit(0);
			_exit(42);
		}
		int status;
		assert(waitpid(pid, &status, 0) == pid);
		assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
		disk(path, check);
		assert(!memcmp(old, check, sizeof(old)));
		assert(!host_card_open(0, path));
		assert(!memcmp(old, card_image_data(0), sizeof(old)));
		host_card_close(0);
	}
	FILE *f = fopen(path, "wb");
	assert(f);
	uint8_t header[64] = { 0 };
	assert(fwrite(header, 1, 64, f) == 64);
	assert(fwrite(old, 1, sizeof(old), f) == sizeof(old));
	fclose(f);
	assert(!host_card_open(0, path));
	assert(!memcmp(old, card_image_data(0), sizeof(old)));
	assert(!card_image_commit(0));
	disk(path, check);
	host_card_close(0);
	for (int size = 64; size <= 200000; size += 199936) {
		f = fopen(path, "wb");
		assert(f);
		assert(!ftruncate(fileno(f), size));
		fclose(f);
		assert(host_card_open(0, path) == -1);
		assert(!card_image_data(0));
		struct stat s;
		assert(!stat(path, &s) && s.st_size == size);
	}
	f = fopen(path, "wb");
	assert(f);
	assert(fwrite(old, 1, sizeof(old), f) == sizeof(old));
	fclose(f);
	assert(!chmod(path, 0));
	assert(host_card_open(0, path) == -1);
	assert(!card_image_data(0));
	chmod(path, 0600);
	disk(path, check);
	assert(!memcmp(old, check, sizeof(old)));
	assert(!host_card_open(0, 0));
	assert(!card_image_data(0));
	unlink(path);
	char pattern[280];
	snprintf(pattern, sizeof(pattern), "%s/*.tmp.*", dir);
	glob_t g = { 0 };
	if (!glob(pattern, 0, 0, &g)) {
		for (size_t i = 0; i < g.gl_pathc; i++) {
			unlink(g.gl_pathv[i]);
		}
	}
	globfree(&g);
	assert(!rmdir(dir));
	puts("card_host: raw/VGS/invalid/permissions and two SIGKILL commit points OK");
	return 0;
}
