/* L1: libcd and the CD model over the synthetic Pack of test_pack (CTest fixture: pack_t1.pack
 * and pack_t.tsv in the cwd). Disc layout and bytes as in port/host/pack/tests/test_pack.c. */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../backend/cdrom.h"
#include "../../host/pack/sha256.h"
#include "../clock.h"
#include "../overlay.h"
#include "../ps1mem.h"
#include "../psyq.h"

#define SEEK (CLOCK_HZ / 10)
#define SECTOR_2X (CLOCK_HZ / 150)
#define SECTOR_1X (CLOCK_HZ / 75)

static int failures;
#define CHECK(cond)                                                                        \
	do {                                                                                   \
		if (!(cond)) {                                                                     \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
			failures++;                                                                    \
		}                                                                                  \
	} while (0)

static char manifest_sha[65];
static uint8_t buf[8 * 2340];

/* Byte k (24..2351) of raw sector lba of the synthetic disc, and its submode. */
static uint8_t raw(uint32_t lba, int k)
{
	return (uint8_t)(lba * 31 + k * 7);
}

static uint8_t submode(uint32_t lba)
{
	return (lba == 27 || lba == 29) ? 0x24 : 0x08;
}

static uint8_t *read_file(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		printf("FAIL: cannot open %s (run after the pack test, its fixture)\n", path);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	*n = (size_t)ftell(f);
	uint8_t *p = malloc(*n);
	fseek(f, 0, SEEK_SET);
	if (p == NULL || fread(p, 1, *n, f) != *n)
		exit(1);
	fclose(f);
	return p;
}

static void write_file(const char *path, const void *p, size_t n)
{
	FILE *f = fopen(path, "wb");
	if (f == NULL || fwrite(p, 1, n, f) != n || fclose(f) != 0)
		exit(1);
}

static void reset(int with_pack)
{
	char err[512];
	clock_reset();
	psyq_libcd_reset();
	cdrom_close();
	if (with_pack && cdrom_open("pack_t1.pack", manifest_sha, err, sizeof(err)) != 0) {
		printf("FAIL: %s\n", err);
		exit(1);
	}
	CdInit();
}

/* Runs fn in a child process; returns whether it aborted. */
static int aborts(void (*fn)(void))
{
	pid_t pid = fork();
	if (pid < 0)
		exit(2);
	if (pid == 0) {
		freopen("/dev/null", "w", stderr);
		setrlimit(RLIMIT_CORE, &(struct rlimit){ 0, 0 }); /* expected abort: no core dump */
		fn();
		_exit(0);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void setloc(int lba)
{
	CdlLOC pos;
	CHECK(CdControl(CdlSetloc, (unsigned char *)CdIntToPos(lba, &pos), NULL) == 1);
}

/* Reads n sectors from lba; returns the cycle at which the read was started. */
static uint64_t start_read(int lba, int n, int mode)
{
	setloc(lba);
	memset(buf, 0xAA, sizeof(buf));
	CHECK(CdRead(n, (unsigned long *)buf, mode) == 1);
	return clock_now();
}

static void positions(void)
{
	CdlLOC p = { 0, 0, 0, 0x55 };
	CHECK(CdIntToPos(0, &p) == &p && p.minute == 0 && p.second == 2 && p.sector == 0 && p.track == 0x55);
	CdIntToPos(10 * 60 * 75 - 150 + 74, &p); /* 10:00:74 */
	CHECK(p.minute == 0x10 && p.second == 0 && p.sector == 0x74);
	for (int i = 0; i < 360000; i += 997) {
		CdIntToPos(i, &p);
		CHECK(CdPosToInt(&p) == i);
	}
}

static void search(void)
{
	reset(1);
	CdlFILE f;
	memset(&f, 0x55, sizeof(f));
	uint64_t t = clock_now();
	CHECK(CdSearchFile(&f, "\\MOVIE\\OP1.STR;1") == &f);
	CHECK(clock_now() - t == CLOCK_PSYQ_ENTRY_CYCLES + SEEK + SECTOR_2X);
	CHECK(CdPosToInt(&f.pos) == 26 && f.size == 4 * 2048 && !strcmp(f.name, "OP1.STR;1"));
	CHECK(f.name[15] == 0);
	CHECK(CdSearchFile(&f, "\\SLUS_010.32;1") == &f);
	CHECK(CdPosToInt(&f.pos) == 22 && f.size == 5000 && !strcmp(f.name, "SLUS_010.32;1"));
	CHECK(CdSearchFile(&f, "\\MOVIE\\OP2.STR;1") == NULL);
	CHECK(CdSearchFile(&f, "\\MOVIE\\OP1.STR") == NULL); /* exact path only */
	CHECK(CdSearchFile(&f, "MOVIE\\OP1.STR;1") == NULL); /* must start with '\' */
}

static void read_form1(void)
{
	reset(1);
	uint64_t t = start_read(22, 3, CdlModeSpeed);
	CHECK(CdReadSync(1, NULL) == 3);
	CHECK(CdReadSync(0, NULL) == 0);
	CHECK(clock_now() == t + SEEK + 3 * SECTOR_2X);
	CHECK(CdReadSync(1, NULL) == 0);
	int ok = 1;
	for (int i = 0; i < 3 * 2048; i++)
		ok &= buf[i] == (i < 5000 ? raw(22 + i / 2048, 24 + i % 2048) : 0xAA);
	CHECK(ok); /* nothing past the end of the file */

	/* The CD model gives zeros in the slack and says how much is file data. */
	uint8_t sector[2048];
	CHECK(cdrom_read_sector(24, sector, 2048) == 5000 - 2 * 2048);
	CHECK(sector[903] == raw(24, 24 + 903) && sector[904] == 0 && sector[2047] == 0);

	/* Single speed, part of a file. */
	t = start_read(23, 1, 0);
	CHECK(CdReadSync(0, NULL) == 0);
	CHECK(clock_now() == t + SEEK + SECTOR_1X);
	CHECK(buf[0] == raw(23, 24) && buf[2047] == raw(23, 24 + 2047) && buf[2048] == 0xAA);
}

static void read_form2(void)
{
	reset(1);
	start_read(26, 4, CdlModeSpeed | CdlModeSize1);
	CHECK(CdReadSync(0, NULL) == 0);
	int ok = 1;
	for (int s = 0; s < 4; s++) {
		const uint8_t *p = buf + s * 2340;
		uint32_t lba = 26 + s;
		/* 26 + 150 = 176 = 00:02:26 */
		ok &= p[0] == 0 && p[1] == 2 && p[2] == (0x26 + s) && p[3] == 2;
		ok &= p[4] == 0 && p[6] == submode(lba) && p[10] == submode(lba) && p[11] == 0;
		for (int j = 0; j < 2328; j++)
			ok &= p[12 + j] == raw(lba, 24 + j);
	}
	CHECK(ok);
	CHECK(buf[4 * 2340] == 0xAA);

	/* The same file with 2048-byte sectors: the data after the subheader. */
	start_read(27, 2, CdlModeSpeed);
	CHECK(CdReadSync(0, NULL) == 0);
	ok = 1;
	for (int i = 0; i < 2 * 2048; i++)
		ok &= buf[i] == raw(27 + i / 2048, 24 + i % 2048);
	CHECK(ok);
}

static uint64_t callback_cycle;
static int callback_calls;
static unsigned char callback_intr;
static int callback_sync = -1;

static void on_read(unsigned char intr, unsigned char *result)
{
	(void)result;
	callback_cycle = clock_now();
	callback_intr = intr;
	callback_calls++;
	callback_sync = CdReadSync(0, NULL); /* from a handler: the read is over, no wait */
}

static void callback(void)
{
	reset(1);
	CHECK(CdReadCallback(on_read) == NULL);
	uint64_t t = start_read(25, 1, CdlModeSpeed);
	clock_advance(SEEK + SECTOR_2X - 1);
	CHECK(callback_calls == 0);
	clock_advance(1);
	CHECK(callback_calls == 1 && callback_intr == CdlComplete && callback_cycle == t + SEEK + SECTOR_2X);
	CHECK(callback_sync == 0);
	CHECK(CdReadSync(1, NULL) == 0);
	CHECK(buf[99] == raw(25, 24 + 99) && buf[100] == 0xAA);
	CHECK(CdReadCallback(NULL) == on_read);
}

static void restart(void)
{
	reset(1);
	start_read(22, 3, CdlModeSpeed);
	clock_advance(SEEK + SECTOR_2X); /* first sector in */
	CHECK(CdReadSync(1, NULL) == 2);
	uint64_t t = start_read(25, 1, CdlModeSpeed); /* abandons the first read */
	CHECK(CdReadSync(1, NULL) == 1);
	CHECK(CdReadSync(0, NULL) == 0);
	CHECK(clock_now() == t + SEEK + SECTOR_2X);
	CHECK(buf[0] == raw(25, 24) && buf[2048] == 0xAA);
}

static void empty_disc(void)
{
	reset(0);
	CdlFILE f;
	CHECK(CdSearchFile(&f, "\\SLUS_010.32;1") == NULL);
	uint64_t t = start_read(22, 2, CdlModeSpeed);
	CHECK(CdReadSync(0, NULL) == 0 && clock_now() == t + SEEK + 2 * SECTOR_2X);
	int ok = 1;
	for (int i = 0; i < 2 * 2048; i++)
		ok &= buf[i] == 0xAA; /* no file data: nothing written */
	CHECK(ok);
	uint8_t sector[2340];
	memset(sector, 0xAA, sizeof(sector));
	CHECK(cdrom_read_sector(22, sector, 2340) == 0 && sector[0] == 0 && sector[2339] == 0);
}

static void read_and_wait(int lba, int mode)
{
	start_read(lba, 1, mode);
	CdReadSync(0, NULL);
}

static void form1_size1(void) { read_and_wait(25, CdlModeSpeed | CdlModeSize1); }
static void outside_index(void) { read_and_wait(20, CdlModeSpeed); } /* root directory */
static void past_disc(void) { read_and_wait(32, CdlModeSpeed); }
static void size0(void) { read_and_wait(22, CdlModeSpeed | CdlModeSize0); }
static void unknown_command(void) { CdControl(CdlSetloc + 1, NULL, NULL); }

static void asserts(void)
{
	reset(1);
	CHECK(aborts(form1_size1));
	CHECK(aborts(outside_index));
	CHECK(aborts(past_disc));
	CHECK(aborts(size0));
	CHECK(aborts(unknown_command));
	CHECK(CdControl(CdlNop, NULL, NULL) == 1 && CdControlB(CdlPause, NULL, NULL) == 1);
	unsigned char mode = CdlModeSpeed;
	CHECK(CdControlF(CdlSetmode, &mode) == 1 && CdSync(0, NULL) == CdlComplete);
}

/* Runtime hook libcd calls after each overlay load (crt.c in dw1). */
static int overlays_done;
void port_overlay_done(void)
{
	overlays_done++;
}

/* Trace lines containing text. */
static int trace_lines(const char *text)
{
	char line[512];
	int n = 0;
	FILE *f = fopen("test_libcd_trace.log", "r");
	if (f == NULL)
		return 0;
	while (fgets(line, sizeof(line), f))
		n += strstr(line, text) != NULL;
	fclose(f);
	return n;
}

/* \MOV_REL.BIN;1 (LBA 30, 2 sectors, 3000 bytes): only a read from its LBA covering it is an
 * overlay load. (A CdlModeSize1 read of it aborts in cdrom first: Form 1.) */
static void overlay(void)
{
	uint8_t *dest = Ps1_Ram + 0x10000, sha[32];
	char want[256], hex[65];
	Sha256 h;

	Sha256_Init(&h);
	for (int i = 0; i < 3000; i++) {
		uint8_t b = raw(30 + i / 2048, 24 + i % 2048);
		Sha256_Update(&h, &b, 1);
	}
	Sha256_Final(&h, sha);
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", sha[i]);
	snprintf(want, sizeof(want), " overlay 7 \\MOV_REL.BIN;1 dest=0x80010000 sha256=%s\n", hex);

	reset(1);
	port_overlay_snapshot_all();
	int lba_n[3][2] = { { 30, 1 }, { 31, 1 }, { 30, 2 } }; /* partial, from an offset, full */
	for (int k = 0; k < 3; k++) {
		setloc(lba_n[k][0]);
		CHECK(CdRead(lba_n[k][1], (unsigned long *)dest, CdlModeSpeed) == 1);
		CHECK(CdReadSync(0, NULL) == 0);
		CHECK(trace_lines(" overlay ") == (k == 2) && overlays_done == (k == 2));
	}
	CHECK(trace_lines(want) == 1 && memcmp(dest, (uint8_t[]){ raw(30, 24) }, 1) == 0);
	setloc(30);
	CdRead(2, (unsigned long *)dest, CdlModeSpeed);
	CHECK(CdReadSync(0, NULL) == 0);
	CHECK(trace_lines(want) == 2 && trace_lines(" overlay ") == 2 && overlays_done == 2);
}

/* pack_open refuses a Pack of another Manifest, version or with a damaged index. */
static void pack_checks(void)
{
	char err[512], other[65];
	size_t n;
	uint8_t *p = read_file("pack_t1.pack", &n);

	strcpy(other, manifest_sha);
	other[0] = other[0] == '0' ? '1' : '0';
	CHECK(cdrom_open("pack_t1.pack", other, err, sizeof(err)) != 0 && strstr(err, "another Manifest"));
	CHECK(cdrom_open("pack_t1.pack", NULL, err, sizeof(err)) == 0);
	static const struct {
		size_t at;
		const char *what;
	} bad[] = {
		{ 8, "format version" },          /* format_version */
		{ 12, "bad header or index" },    /* count */
		{ 64 + 64, "bad header or index" }, /* lba of the first entry: overlaps the second */
		{ 64 + 128 + 80, "bad header or index" }, /* offset of the second entry */
		{ 64 + 128 + 120, "bad header or index" }, /* reserved bytes */
	};
	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		p[bad[i].at] ^= 1;
		write_file("libcd_bad.pack", p, n);
		p[bad[i].at] ^= 1;
		CHECK(cdrom_open("libcd_bad.pack", manifest_sha, err, sizeof(err)) != 0 && strstr(err, bad[i].what));
	}
	write_file("libcd_bad.pack", p, n - 1);
	CHECK(cdrom_open("libcd_bad.pack", manifest_sha, err, sizeof(err)) != 0 && strstr(err, "truncated"));
	free(p);
}

int main(void)
{
	size_t n;
	uint8_t *tsv = read_file("pack_t.tsv", &n), sha[32];
	Sha256 h;

	setenv("DW1_TRACE", "test_libcd_trace.log", 1);
	remove("test_libcd_trace.log");
	Sha256_Init(&h);
	Sha256_Update(&h, tsv, n);
	Sha256_Final(&h, sha);
	free(tsv);
	for (int i = 0; i < 32; i++)
		snprintf(manifest_sha + i * 2, 3, "%02x", sha[i]);

	positions();
	search();
	read_form1();
	read_form2();
	callback();
	restart();
	empty_disc();
	asserts();
	overlay();
	pack_checks();
	printf(failures ? "test_libcd: %d failures\n" : "test_libcd: OK\n", failures);
	return failures != 0;
}
