/* dw1-pack: Manifest and Pack of the user's Digimon World USA disc (port/host/pack/pack.h).
 * Exit status: 0 OK, 1 the input failed a check, 2 usage. */
#include <stdio.h>
#include <string.h>

#include "pack.h"
#include "sha256.h"

int main(int argc, char **argv)
{
	char err[1024];
	const char *cmd = argc > 1 ? argv[1] : "";
	int r;

	if (strcmp(cmd, "sha256") == 0 && argc == 3) {
		static unsigned char buf[65536];
		unsigned char digest[32];
		Sha256 hash;
		size_t n, i;
		FILE *f = fopen(argv[2], "rb");

		if (f == NULL) {
			fprintf(stderr, "cannot open %s\n", argv[2]);
			return 1;
		}
		Sha256_Init(&hash);
		while ((n = fread(buf, 1, sizeof(buf), f)) != 0) {
			Sha256_Update(&hash, buf, n);
		}
		r = ferror(f);
		fclose(f);
		if (r) {
			fprintf(stderr, "cannot read %s\n", argv[2]);
			return 1;
		}
		Sha256_Final(&hash, digest);
		for (i = 0; i < sizeof(digest); i++) {
			printf("%02x", digest[i]);
		}
		printf("\n");
		return 0;
	} else if (strcmp(cmd, "manifest") == 0 && argc == 4) {
		r = pack_manifest(argv[2], argv[3], err, sizeof(err));
	} else if (strcmp(cmd, "build") == 0 && argc == 5) {
		r = pack_build(argv[2], argv[3], argv[4], err, sizeof(err));
	} else if (strcmp(cmd, "verify") == 0 && argc == 4) {
		r = pack_verify(argv[2], argv[3], err, sizeof(err));
	} else {
		fprintf(stderr, "usage: dw1-pack manifest <image.cue|.bin> <manifest.tsv>\n"
		                "       dw1-pack build <image.cue|.bin> <manifest.tsv> <out.pack>\n"
		                "       dw1-pack verify <file.pack> <manifest.tsv>\n"
		                "       dw1-pack sha256 <file>\n");
		return 2;
	}
	if (r != 0) {
		fprintf(stderr, "dw1-pack %s: %s\n", cmd, err);
		return 1;
	}
	printf("dw1-pack %s: OK\n", cmd);
	return 0;
}
