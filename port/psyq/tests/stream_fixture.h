/* Synthetic ISO fixture helpers, shared model with pack/tests/test_pack.c. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pack.h"
#include "sha256.h"

#define RAW 2352
#define SECTORS 32
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			exit(1);                                                          \
		}                                                                         \
	} while (0)

static uint8_t disc[SECTORS * RAW];
static char err[1024];

static void write_file(const char *path, const void *data, size_t n)
{
	FILE *f = fopen(path, "wb");

	CHECK(f != NULL && fwrite(data, 1, n, f) == n && fclose(f) == 0);
}

static uint8_t *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *p;

	CHECK(f != NULL && fseek(f, 0, SEEK_END) == 0);
	*n = (size_t)ftell(f);
	p = malloc(*n + 1);
	CHECK(p != NULL && fseek(f, 0, SEEK_SET) == 0 && fread(p, 1, *n, f) == *n);
	fclose(f);
	p[*n] = 0;
	return p;
}

static uint8_t bcd(int v)
{
	return (uint8_t)(v / 10 << 4 | v % 10);
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* An ISO9660 directory record at d; returns its length. */
static int dir_record(uint8_t *d, const char *name, uint32_t lba, uint32_t size, int is_dir)
{
	int n = name[0] == 0 ? 1 : (int)strlen(name), len = (33 + n + 1) & ~1;

	d[0] = (uint8_t)len;
	wr32(d + 2, lba);
	wr32(d + 10, size);
	d[25] = is_dir ? 2 : 0;
	d[32] = (uint8_t)n;
	memcpy(d + 33, name, n);
	return len;
}

static uint8_t *user(int lba)
{
	return disc + lba * RAW + 24;
}

/*
 * Sector 16 PVD, 20 root, 21 \MOVIE, 22-24 \SLUS_010.32;1 (5000 bytes), 25 \SYSTEM.CNF;1 (100),
 * 26-29 \MOVIE\OP1.STR;1 (4 sectors, 27 and 29 Form 2), 30-31 \MOV_REL.BIN;1 (3000, an overlay for
 * test_libcd).
 */
static void make_disc(void)
{
	static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	int i, p;

	for (i = 0; i < SECTORS; i++) {
		uint8_t *s = disc + i * RAW;
		int a = i + 150, k;

		memcpy(s, sync, 12);
		s[12] = bcd(a / 4500), s[13] = bcd(a / 75 % 60), s[14] = bcd(a % 75), s[15] = 2;
		s[18] = s[22] = (i == 27 || i == 29) ? 0x24 : 0x08;
		for (k = 24; k < RAW; k++) {
			s[k] = (uint8_t)(i * 31 + k * 7);
		}
	}
	for (i = 16; i <= 21; i++) {
		memset(user(i), 0, 2048);
	}
	user(16)[0] = 1;
	memcpy(user(16) + 1, "CD001", 5);
	dir_record(user(16) + 156, "\0", 20, 2048, 1);
	p = dir_record(user(20), "\0", 20, 2048, 1);
	p += dir_record(user(20) + p, "\1", 20, 2048, 1);
	p += dir_record(user(20) + p, "MOVIE", 21, 2048, 1);
	p += dir_record(user(20) + p, "SLUS_010.32;1", 22, 5000, 0);
	p += dir_record(user(20) + p, "SYSTEM.CNF;1", 25, 100, 0);
	dir_record(user(20) + p, "MOV_REL.BIN;1", 30, 3000, 0);
	p = dir_record(user(21), "\0", 21, 2048, 1);
	p += dir_record(user(21) + p, "\1", 20, 2048, 1);
	dir_record(user(21) + p, "OP1.STR;1", 26, 4 * 2048, 0);
	write_file("pack_t.bin", disc, sizeof(disc));
}

static void write_cue(const char *mode)
{
	char cue[128];

	snprintf(cue, sizeof(cue), "FILE \"pack_t.bin\" BINARY\n  TRACK 01 %s\n    INDEX 01 00:00:00\n", mode);
	write_file("pack_t.cue", cue, strlen(cue));
}

