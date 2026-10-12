/* Native headless host: runs the game without a window (tests, AD-14). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../backend/cdrom.h"
#include "../../psyq/crt.h"

/* SHA-256 of tools/port/manifest/<version>.tsv, set by CMake: only a Pack built from it opens. */
#ifndef DW1_MANIFEST_SHA256
#error "DW1_MANIFEST_SHA256 must be defined by the build"
#endif

static int usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [--headless] [--pack FILE] [--frames N] [--until-overlay]\n"
	                "  --pack FILE      the Pack of your disc (dw1-pack build); without it the disc is empty\n"
	                "  --frames N       exit with status 0 after N frames\n"
	                "  --until-overlay  exit with status 0 once the first overlay is loaded\n",
	        argv0);
	return 2;
}

int main(int argc, char **argv)
{
	long frames = 0;
	int until_overlay = 0;
	const char *pack = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--headless"))
			continue;
		if (!strcmp(argv[i], "--until-overlay")) {
			until_overlay = 1;
			continue;
		}
		if (!strcmp(argv[i], "--pack") && i + 1 < argc) {
			pack = argv[++i];
			continue;
		}
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
			char *end;
			frames = strtol(argv[++i], &end, 10);
			if (*end == '\0' && frames > 0)
				continue;
		}
		return usage(argv[0]);
	}
	if (pack) {
		char err[1024];
		if (cdrom_open(pack, DW1_MANIFEST_SHA256, err, sizeof(err)) != 0) {
			fprintf(stderr, "dw1: %s\n", err);
			return 1;
		}
	}
	return port_run_game(frames, until_overlay);
}
