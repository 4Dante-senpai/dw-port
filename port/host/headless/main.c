/* Native headless host: runs the game without a window (tests, AD-14). */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../backend/cdrom.h"
#include "../../psyq/crt.h"
#include "../../psyq/libgte.h"
#include "../pack/sha256.h"

/* SHA-256 of tools/port/manifest/<version>.tsv, set by CMake: only a Pack built from it opens. */
#ifndef DW1_MANIFEST_SHA256
#error "DW1_MANIFEST_SHA256 must be defined by the build"
#endif

static int usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [--headless] [--pack FILE] [--frames N] [--until-overlay]\n"
	                "  --pack FILE      the Pack of your disc (dw1-pack build); without it the disc is empty\n"
	                "  --frames N       exit with status 0 after N frames\n"
	                "  --capture-mark NAME --capture-vblank K --capture FILE  capture after mark + K VBlanks\n"
	                "  --clock-trace FILE  write deterministic clock events\n"
	                "  --inputs FILE --inputs-from frame0|mark  latch wire button masks per VBlank\n"
	                "  --checkpoints FILE  capture ordered symbol[#n] entries and VBlank windows\n"
	                "  --state-vars FILE --state-trace FILE  record typed state and rand outputs\n"
	                "  --audio-segments FILE  capture SPU trace/WAV segments instead of an image\n"
	                "  --until-overlay  exit with status 0 once the first overlay is loaded\n",
	        argv0);
	return 2;
}

int main(int argc, char **argv)
{
	long frames = 0;
	PortCapture config = {0};
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
		if (i + 1 < argc) {
			if (!strcmp(argv[i], "--audio-segments")) { config.audio_segments = argv[++i]; continue; }
			if (!strcmp(argv[i], "--inputs")) { config.inputs = argv[++i]; continue; }
			if (!strcmp(argv[i], "--checkpoints")) { config.checkpoints = argv[++i]; continue; }
			if (!strcmp(argv[i], "--inputs-from")) { config.inputs_from = argv[++i]; continue; }
			if (!strcmp(argv[i], "--state-vars")) { config.state_vars = argv[++i]; continue; }
			if (!strcmp(argv[i], "--state-trace")) { config.state_trace = argv[++i]; continue; }
			if (!strcmp(argv[i], "--capture-mark")) {
				config.mark = argv[++i];
				continue;
			}
			if (!strcmp(argv[i], "--capture")) {
				config.capture = argv[++i];
				continue;
			}
			if (!strcmp(argv[i], "--clock-trace")) {
				config.clock_trace = argv[++i];
				continue;
			}
			if (!strcmp(argv[i], "--capture-vblank")) {
				char *end;
				const char *value = argv[++i];
				errno = 0;
				config.vblank = strtoul(value, &end, 10);
				if (!errno && *value >= '0' && *value <= '9' && !*end &&
				    config.vblank > 0 && config.vblank <= UINT_MAX)
					continue;
				return usage(argv[0]);
			}
		}
		if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
			char *end;
			frames = strtol(argv[++i], &end, 10);
			if (*end == '\0' && frames > 0)
				continue;
		}
		return usage(argv[0]);
	}
	if (config.audio_segments && (!*config.audio_segments || !config.mark || !*config.mark ||
	    !config.checkpoints || !*config.checkpoints || config.capture || config.state_vars || config.vblank))
		return usage(argv[0]);
	if (!config.audio_segments && (config.mark || config.capture || config.vblank) &&
	    (!config.mark || !*config.mark || !config.capture || !*config.capture || !config.vblank))
		return usage(argv[0]);
	if ((config.inputs_from && strcmp(config.inputs_from, "mark") && strcmp(config.inputs_from, "frame0")) ||
	    (!!config.state_vars != !!config.state_trace)) return usage(argv[0]);
	if ((config.checkpoints || config.state_vars || (config.inputs_from && !strcmp(config.inputs_from, "mark"))) && !config.mark)
		return usage(argv[0]);
	if (pack) {
		char err[1024];
		if (cdrom_open(pack, DW1_MANIFEST_SHA256, err, sizeof(err)) != 0) {
			fprintf(stderr, "dw1: %s\n", err);
			return 1;
		}
		if (libgte_load_tables(err, sizeof(err)) != 0) {
			fprintf(stderr, "dw1: %s\n", err);
			cdrom_close();
			return 1;
		}
		const char *names[] = {"rsin_tbl", "SQRT", "rcossin_tbl", "ratan_tbl"};
		for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			size_t size;
			const void *data = libgte_table(names[i], &size);
			Sha256 hash;
			uint8_t digest[32];
			Sha256_Init(&hash); Sha256_Update(&hash, data, size); Sha256_Final(&hash, digest);
			printf("port: libgte table %s sha256 ", names[i]);
			for (unsigned j = 0; j < sizeof(digest); j++) printf("%02x", digest[j]);
			printf("\n");
		}
	}
	return port_run_game(frames, until_overlay, &config);
}
