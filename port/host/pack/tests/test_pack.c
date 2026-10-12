/* L1: Manifest, Pack build and verify on a synthetic disc (no disc bytes). Runs in the cwd. */
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

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
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
 * 26-29 \MOVIE\OP1.STR;1 (4 sectors, 27 and 29 Form 2).
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
	dir_record(user(20) + p, "SYSTEM.CNF;1", 25, 100, 0);
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

static int contains(const char *s, const char *sub)
{
	if (strstr(s, sub) == NULL) {
		fprintf(stderr, "'%s' not in: %s\n", sub, s);
		return 0;
	}
	return 1;
}

/* A build that must fail: err names what, and neither the Pack nor its temporary exists. */
static void build_fails(const char *tsv, const char *what)
{
	remove("pack_t_bad.pack");
	CHECK(pack_build("pack_t.cue", tsv, "pack_t_bad.pack", err, sizeof(err)) != 0);
	CHECK(contains(err, what));
	CHECK(access("pack_t_bad.pack", F_OK) != 0 && access("pack_t_bad.pack.tmp", F_OK) != 0);
}

static void test_sha256(void)
{
	static const char *msg[2] = { "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" };
	static const uint8_t want[2][32] = {
		{ 0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
		  0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad },
		{ 0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
		  0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1 },
	};
	int i;

	for (i = 0; i < 2; i++) {
		uint8_t d[32];
		Sha256 s;

		Sha256_Init(&s);
		Sha256_Update(&s, msg[i], strlen(msg[i]));
		Sha256_Final(&s, d);
		CHECK(memcmp(d, want[i], 32) == 0);
	}
}

/* Header, index and data of the Pack of the synthetic disc, byte for byte. */
static void check_layout(const uint8_t *p, size_t n)
{
	static const struct {
		const char *path;
		uint32_t lba, sectors, mode, size, offset;
	} want[3] = {
		{ "\\SLUS_010.32;1", 22, 3, PACK_F1, 5000, 2048 },
		{ "\\SYSTEM.CNF;1", 25, 1, PACK_F1, 100, 8192 },
		{ "\\MOVIE\\OP1.STR;1", 26, 4, PACK_F2, 4 * 2336, 10240 },
	};
	size_t tsv_n;
	uint8_t *tsv = read_file("pack_t.tsv", &tsv_n), sha[32];
	uint32_t pos = PACK_HEADER_SIZE + 3 * PACK_ENTRY_SIZE;
	Sha256 s;
	int i, k;

	Sha256_Init(&s);
	Sha256_Update(&s, tsv, tsv_n);
	Sha256_Final(&s, sha);
	free(tsv);
	CHECK(n == 10240 + 4 * 2336);
	CHECK(memcmp(p, "DW1PACK\0", 8) == 0 && rd32(p + 8) == 1 && rd32(p + 12) == 3);
	CHECK(memcmp(p + 16, sha, 32) == 0 && rd32(p + 48) == 64 && rd32(p + 52) == 2048);
	for (i = 0; i < 3; i++) {
		const uint8_t *e = p + 64 + i * 128, *d = p + want[i].offset;

		CHECK(strncmp((const char *)e, want[i].path, 64) == 0);
		CHECK(rd32(e + 64) == want[i].lba && rd32(e + 68) == want[i].sectors && rd32(e + 72) == want[i].mode);
		CHECK(rd32(e + 76) == want[i].size && rd32(e + 80) == want[i].offset);
		Sha256_Init(&s);
		Sha256_Update(&s, d, want[i].size);
		Sha256_Final(&s, sha);
		CHECK(memcmp(e + 84, sha, 32) == 0);
		for (; pos < want[i].offset; pos++) {
			CHECK(p[pos] == 0);
		}
		for (k = 0; k < (int)want[i].sectors; k++) {
			const uint8_t *raw = disc + (want[i].lba + k) * RAW;

			if (want[i].mode == PACK_F2) {
				CHECK(memcmp(d + k * 2336, raw + 16, 2336) == 0);
			} else {
				uint32_t m = want[i].size - k * 2048 < 2048 ? want[i].size - k * 2048 : 2048;

				CHECK(memcmp(d + k * 2048, raw + 24, m) == 0);
			}
		}
		pos += want[i].size;
	}
}

/* The Manifest with lines removed (skip) or added (extra) after the line starting with at. */
static void edit_manifest(const char *out, const char *at, int skip, const char *extra)
{
	size_t n;
	char *t = (char *)read_file("pack_t.tsv", &n), *p = strstr(t, at), *nl;
	FILE *f = fopen(out, "wb");

	CHECK(p != NULL && f != NULL);
	nl = strchr(p, '\n') + 1;
	fwrite(t, 1, (size_t)((skip ? p : nl) - t), f);
	fputs(extra, f);
	fputs(nl, f);
	CHECK(fclose(f) == 0);
	free(t);
}

int main(void)
{
	uint8_t *p1, *p2;
	size_t n1, n2;
	char *tsv;

	test_sha256();
	make_disc();
	write_cue("MODE2/2352");

	/* Manifest: @exe first, then the files by LBA, F2 for the file with Form 2 sectors. */
	CHECK(pack_manifest("pack_t.cue", "pack_t.tsv", err, sizeof(err)) == 0);
	tsv = (char *)read_file("pack_t.tsv", &n1);
	CHECK(contains(tsv, "\n@exe\t22\t3\tF1\t5000\t"));
	CHECK(contains(tsv, "\n\\SLUS_010.32;1\t22\t3\tF1\t5000\t"));
	CHECK(contains(tsv, "\n\\SYSTEM.CNF;1\t25\t1\tF1\t100\t"));
	CHECK(contains(tsv, "\n\\MOVIE\\OP1.STR;1\t26\t4\tF2\t9344\t"));
	CHECK(strstr(tsv, "\\SLUS") < strstr(tsv, "\\SYSTEM") && strstr(tsv, "\\SYSTEM") < strstr(tsv, "\\MOVIE"));
	free(tsv);
	/* The raw .bin alone gives the same Manifest. */
	CHECK(pack_manifest("pack_t.bin", "pack_t_bin.tsv", err, sizeof(err)) == 0);
	p1 = read_file("pack_t.tsv", &n1);
	p2 = read_file("pack_t_bin.tsv", &n2);
	CHECK(n1 == n2 && memcmp(p1, p2, n1) == 0);
	free(p1);
	free(p2);

	/* Build twice: same bytes, the expected layout, verify OK. */
	CHECK(pack_build("pack_t.cue", "pack_t.tsv", "pack_t1.pack", err, sizeof(err)) == 0);
	CHECK(pack_build("pack_t.cue", "pack_t.tsv", "pack_t2.pack", err, sizeof(err)) == 0);
	CHECK(access("pack_t1.pack.tmp", F_OK) != 0);
	p1 = read_file("pack_t1.pack", &n1);
	p2 = read_file("pack_t2.pack", &n2);
	CHECK(n1 == n2 && memcmp(p1, p2, n1) == 0);
	check_layout(p1, n1);
	CHECK(pack_verify("pack_t1.pack", "pack_t.tsv", err, sizeof(err)) == 0);

	/* Corrupt Pack: verify names the file. */
	p1[10240 + 3000] ^= 1;
	write_file("pack_t_bad.pack", p1, n1);
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "\\MOVIE\\OP1.STR;1"));
	p1[10240 + 3000] ^= 1;
	p1[64 + 128 + 5] ^= 1; /* index path of \SYSTEM.CNF;1 */
	write_file("pack_t_bad.pack", p1, n1);
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "\\SYSTEM.CNF;1"));
	p1[64 + 128 + 5] ^= 1;
	p1[7500] = 1; /* padding before \SYSTEM.CNF;1 */
	write_file("pack_t_bad.pack", p1, n1);
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "\\SYSTEM.CNF;1"));
	p1[7500] = 0;
	p1[8] ^= 1; /* format_version */
	write_file("pack_t_bad.pack", p1, n1);
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "bad header"));
	p1[8] ^= 1;
	write_file("pack_t_bad.pack", p1, n1 - 1); /* truncated */
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "\\MOVIE\\OP1.STR;1"));
	{
		uint8_t *longer = calloc(n1 + 1, 1); /* one trailing byte */

		CHECK(longer != NULL);
		memcpy(longer, p1, n1);
		write_file("pack_t_bad.pack", longer, n1 + 1);
		free(longer);
	}
	CHECK(pack_verify("pack_t_bad.pack", "pack_t.tsv", err, sizeof(err)) != 0);
	CHECK(contains(err, "bytes after the last file"));
	free(p1);
	free(p2);

	/* A byte changed in a file. */
	user(25)[10] ^= 1;
	write_file("pack_t.bin", disc, sizeof(disc));
	build_fails("pack_t.tsv", "\\SYSTEM.CNF;1");
	user(25)[10] ^= 1;
	disc[27 * RAW + 2000] ^= 1; /* Form 2 data of OP1.STR */
	write_file("pack_t.bin", disc, sizeof(disc));
	build_fails("pack_t.tsv", "\\MOVIE\\OP1.STR;1");
	disc[27 * RAW + 2000] ^= 1;

	/* Another exe: only USA. */
	user(23)[0] ^= 1;
	write_file("pack_t.bin", disc, sizeof(disc));
	build_fails("pack_t.tsv", "only the USA version of Digimon World (SLUS-01032)");
	CHECK(contains(err, "\\SLUS_010.32;1"));
	user(23)[0] ^= 1;
	write_file("pack_t.bin", disc, sizeof(disc));

	/* A file missing from the Manifest, or one the disc does not have. */
	edit_manifest("pack_t_less.tsv", "\\SYSTEM.CNF;1", 1, "");
	build_fails("pack_t_less.tsv", "\\SYSTEM.CNF;1");
	edit_manifest("pack_t_more.tsv", "\\MOVIE\\OP1.STR;1", 0,
	              "\\MOVIE\\OP2.STR;1\t30\t1\tF1\t10\t0000000000000000000000000000000000000000000000000000000000000000\n");
	build_fails("pack_t_more.tsv", "\\MOVIE\\OP2.STR;1");

	/* Track mode of the .cue, and a 2048-byte ISO. */
	write_cue("MODE1/2352");
	build_fails("pack_t.tsv", "MODE1/2352");
	write_cue("MODE2/2352");
	{
		static uint8_t iso[20 * 2048];

		memcpy(iso + 16 * 2048, user(16), 2048);
		write_file("pack_t.iso", iso, sizeof(iso));
		CHECK(pack_build("pack_t.iso", "pack_t.tsv", "pack_t_bad.pack", err, sizeof(err)) != 0);
		CHECK(contains(err, "XA audio and the video"));
		CHECK(access("pack_t_bad.pack", F_OK) != 0 && access("pack_t_bad.pack.tmp", F_OK) != 0);
	}

	/* A Manifest with no file rows. */
	{
		size_t n;
		char *t = (char *)read_file("pack_t.tsv", &n);

		write_file("pack_t_empty.tsv", t, (size_t)(strstr(t, "\n\\SLUS") + 1 - t));
		free(t);
		build_fails("pack_t_empty.tsv", "no file rows");
		CHECK(pack_verify("pack_t1.pack", "pack_t_empty.tsv", err, sizeof(err)) != 0);
		CHECK(contains(err, "no file rows"));
	}

	/* A quoted FILE name with a space, relative to the .cue's folder. */
	{
		static const char cue[] = "FILE \"pack t.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";

		mkdir("pack_sub", 0777);
		write_file("pack_sub/pack t.bin", disc, sizeof(disc));
		write_file("pack_sub/pack t.cue", cue, strlen(cue));
		CHECK(pack_build("pack_sub/pack t.cue", "pack_t.tsv", "pack_t3.pack", err, sizeof(err)) == 0);
	}

	/* Still builds after all the restores. */
	CHECK(pack_build("pack_t.cue", "pack_t.tsv", "pack_t1.pack", err, sizeof(err)) == 0);
	puts("test_pack: OK");
	return 0;
}
