/* Native headless host: runs the game without a window (tests, AD-14). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../psyq/crt.h"

int main(int argc, char **argv)
{
	long frames = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--headless"))
			continue;
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
			char *end;
			frames = strtol(argv[++i], &end, 10);
			if (*end == '\0' && frames > 0)
				continue;
		}
		fprintf(stderr, "usage: %s [--headless] [--frames N]\n", argv[0]);
		return 2;
	}
	return port_run_game(frames);
}
