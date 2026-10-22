/* Native raw memory card persistence (AD-13). */
#include "card.h"
#include "../psyq/card_image.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static char *paths[2];
/* Test-only crash injection: never active unless explicitly requested. */
static void cut(const char *point)
{
	const char *requested = getenv("DW1_CARD_TEST_KILL");
	if (requested && !strcmp(requested, point)) {
		kill(getpid(), SIGKILL);
	}
}
static int commit(void *context, const uint8_t *image, size_t size)
{
	const char *path = context;
	char *tmp = malloc(strlen(path) + 16);
	if (!tmp) {
		return -1;
	}
	sprintf(tmp, "%s.tmp.XXXXXX", path);
	int fd = mkstemp(tmp), result = -1;
	if (fd < 0) {
		goto done;
	}
	size_t offset = 0;
	while (offset < size) {
		size_t count = size - offset;
		if (!offset) {
			count /= 2;
		}
		ssize_t n = write(fd, image + offset, count);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			goto close_file;
		}
		offset += (size_t)n;
		cut("during-temp");
	}
	if (fsync(fd)) {
		goto close_file;
	}
	if (close(fd)) {
		fd = -1;
		goto close_file;
	}
	fd = -1;
	cut("before-rename");
	if (rename(tmp, path)) {
		goto close_file;
	}
	/* Also make the directory entry durable across power loss. */
	char *directory = strdup(path);
	if (directory) {
		char *slash = strrchr(directory, '/');
		if (slash) {
			if (slash == directory) {
				slash[1] = 0;
			} else {
				*slash = 0;
			}
		} else {
			free(directory);
			directory = strdup(".");
		}
		int dfd = directory ? open(directory, O_RDONLY | O_DIRECTORY) : -1;
		if (dfd >= 0) {
			result = fsync(dfd);
			close(dfd);
		}
		free(directory);
	}
close_file:
	if (fd >= 0) {
		close(fd);
	}
	if (result) {
		fprintf(stderr, "memory card: commit failed for %s: %s\n", path, strerror(errno));
	}
	unlink(tmp);
done:
	free(tmp);
	return result;
}
void host_card_close(int port)
{
	if (port < 0 || port > 1) {
		return;
	}
	card_image_detach(port);
	free(paths[port]);
	paths[port] = NULL;
}
int host_card_open(int port, const char *path)
{
	if (port < 0 || port > 1) {
		return -1;
	}
	host_card_close(port);
	if (!path) {
		return 0;
	}
	uint8_t image[CARD_IMAGE_SIZE];
	struct stat status;
	FILE *file = fopen(path, "rb");
	if (!file) {
		if (errno != ENOENT) {
			goto invalid;
		}
		card_image_new(image);
		if (commit((void *)path, image, sizeof(image))) {
			return -1;
		}
	} else {
		if (fstat(fileno(file), &status) || !S_ISREG(status.st_mode) ||
		    !(status.st_mode & 0444) || (status.st_size != CARD_IMAGE_SIZE && status.st_size != CARD_IMAGE_SIZE + 64)) {
			fclose(file);
			goto invalid;
		}
		if (status.st_size == CARD_IMAGE_SIZE + 64 && fseek(file, 64, SEEK_SET)) {
			fclose(file);
			goto invalid;
		}
		if (fread(image, 1, sizeof(image), file) != sizeof(image)) {
			fclose(file);
			goto invalid;
		}
		fclose(file);
	}
	paths[port] = strdup(path);
	if (!paths[port]) {
		return -1;
	}
	card_image_attach(port, image, commit, paths[port]);
	return 0;
invalid:
	fprintf(stderr, "memory card: cannot load %s (expected 131072 raw or 131136 VGS bytes); slot %d absent\n", path, port + 1);
	return -1;
}
