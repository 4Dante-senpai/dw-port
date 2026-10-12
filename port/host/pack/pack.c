/* Disc image, ISO9660, Manifest and Pack (see pack.h). Cue parsing and the 2048-byte ISO check
 * follow DW2-PC host/pakbuild.c @ 816ed0d (CC0), without SDL. */
#define _FILE_OFFSET_BITS 64

#include "pack.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#include "../../psyq/port.h"
#include "sha256.h"

#define RAW 2352u
#define DATA 2048u
#define MAX_SECTORS (1u << 20) /* far more than any CD; keeps every size and offset in 32 bits */
#define MAX_DEPTH 8            /* ISO9660 directory depth limit */
#define MAX_TEXT (4u << 20)    /* largest .cue / Manifest read */

typedef PackEntry Entry;

typedef struct {
	Entry *e;
	size_t n, cap;
} List;

typedef struct {
	char *err;
	size_t errlen;
	FILE *img;
	uint32_t img_sectors;
	uint32_t next; /* sector the image file is positioned at */
} Ctx;

static int fail(Ctx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int fail(Ctx *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(c->err, c->errlen, fmt, ap);
	va_end(ap);
	return -1;
}

static uint32_t rd32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

static uint32_t align(uint64_t v)
{
	return (uint32_t)((v + PACK_ALIGN - 1) / PACK_ALIGN * PACK_ALIGN);
}

static int list_add(Ctx *c, List *l, const Entry *e)
{
	if (l->n == l->cap) {
		size_t cap = l->cap ? l->cap * 2 : 1024;
		Entry *p = realloc(l->e, cap * sizeof(*p));

		if (p == NULL) {
			return fail(c, "out of memory");
		}
		l->e = p, l->cap = cap;
	}
	l->e[l->n++] = *e;
	return 0;
}

static int entry_cmp(const void *a, const void *b)
{
	const Entry *x = a, *y = b;

	if (x->lba != y->lba) {
		return x->lba < y->lba ? -1 : 1;
	}
	return strcmp(x->path, y->path);
}

/* e comes after prev in a Manifest or index: sorted by LBA and path, sectors not shared (each
 * LBA belongs to at most one file, as the CD model looks files up by LBA). */
static int follows(const Entry *prev, const Entry *e)
{
	return entry_cmp(prev, e) < 0 && (uint64_t)prev->lba + prev->sectors <= e->lba;
}

/* A whole small text file (.cue, Manifest), NUL-terminated; malloc'd. */
static char *read_text(Ctx *c, const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	char *text;
	long n;

	if (f == NULL) {
		fail(c, "cannot open %s", path);
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || (size_t)n > MAX_TEXT ||
	    fseek(f, 0, SEEK_SET) != 0 || (text = malloc((size_t)n + 1)) == NULL) {
		fclose(f);
		fail(c, "cannot read %s (larger than %u bytes or unreadable)", path, MAX_TEXT);
		return NULL;
	}
	if (fread(text, 1, (size_t)n, f) != (size_t)n) {
		free(text);
		fclose(f);
		fail(c, "cannot read %s", path);
		return NULL;
	}
	fclose(f);
	text[n] = 0;
	*size = (size_t)n;
	return text;
}

/* ---- disc image ---- */

static int has_ext(const char *path, const char *ext)
{
	size_t n = strlen(path), k = strlen(ext);

	return n >= k && strcasecmp(path + n - k, ext) == 0;
}

/* Next whitespace-separated or "quoted" token of a .cue line. */
static const char *cue_token(const char *p, char *out, size_t n)
{
	size_t k = 0;
	int quoted;

	while (*p == ' ' || *p == '\t') {
		p++;
	}
	quoted = *p == '"';
	p += quoted;
	while (*p != 0 && *p != '\n' && *p != '\r' && (quoted ? *p != '"' : *p != ' ' && *p != '\t')) {
		if (k + 1 < n) {
			out[k++] = *p;
		}
		p++;
	}
	p += quoted && *p == '"';
	out[k] = 0;
	return p;
}

/* The data file of a .cue: its first FILE (BINARY), whose first TRACK must be 01 MODE2/2352. */
static int cue_data_file(Ctx *c, const char *cue, char *bin, size_t n)
{
	size_t size;
	char *text = read_text(c, cue, &size);
	const char *p = text;
	char tok[1024];
	int file_seen = 0;
	int r = -1;

	if (text == NULL) {
		return -1;
	}
	if (size >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) {
		p += 3;
	}
	for (; *p != 0; p += *p == '\n') {
		const char *q = cue_token(p, tok, sizeof(tok));

		if (strcasecmp(tok, "FILE") == 0 && !file_seen) {
			char name[1024], type[32];
			const char *slash = strrchr(cue, '/');
			int dir = slash != NULL ? (int)(slash + 1 - cue) : 0;

			q = cue_token(q, name, sizeof(name));
			cue_token(q, type, sizeof(type));
			if (strcasecmp(type, "BINARY") != 0) {
				fail(c, "%s: its first file \"%s\" is %s, not a BINARY raw dump", cue, name, type);
				goto out;
			}
			/* A relative name is relative to the .cue's folder. */
			if (snprintf(bin, n, "%.*s%s", name[0] == '/' ? 0 : dir, cue, name) >= (int)n) {
				fail(c, "%s: the file path is too long", cue);
				goto out;
			}
			file_seen = 1;
		} else if (strcasecmp(tok, "TRACK") == 0 && file_seen) {
			char num[16], mode[32];

			q = cue_token(q, num, sizeof(num));
			cue_token(q, mode, sizeof(mode));
			if (strcmp(num, "01") != 0 && strcmp(num, "1") != 0) {
				fail(c, "%s: the first track is %s, not 01", cue, num);
			} else if (strcasecmp(mode, "MODE2/2352") != 0) {
				fail(c, "%s: track 01 is %s; the Digimon World data track is MODE2/2352 (make a raw "
				        "dump of the disc)",
				     cue, mode);
			} else {
				r = 0;
			}
			goto out;
		}
		while (*p != 0 && *p != '\n') {
			p++;
		}
	}
	fail(c, file_seen ? "%s: no TRACK after its FILE" : "%s: names no FILE", cue);
out:
	free(text);
	return r;
}

/* Opens a .cue's data file, or the file itself, as an image of raw 2352-byte sectors. */
static int image_open(Ctx *c, const char *path)
{
	char bin[1024];
	uint8_t s[6];
	off_t size;

	if (has_ext(path, ".cue")) {
		if (cue_data_file(c, path, bin, sizeof(bin)) != 0) {
			return -1;
		}
	} else if (snprintf(bin, sizeof(bin), "%s", path) >= (int)sizeof(bin)) {
		return fail(c, "the image path is too long");
	}
	c->img = fopen(bin, "rb");
	if (c->img == NULL) {
		return fail(c, "cannot open %s", bin);
	}
	if (fseeko(c->img, 0, SEEK_END) != 0 || (size = ftello(c->img)) < 0) {
		return fail(c, "cannot read %s", bin);
	}
	/* A 2048-byte ISO: the ISO9660 descriptor right at 16 * 2048. */
	if (size >= 17 * DATA && fseeko(c->img, 16 * DATA, SEEK_SET) == 0 &&
	    fread(s, 1, 6, c->img) == 6 && memcmp(s + 1, "CD001", 5) == 0) {
		return fail(c, "%s is a 2048-byte-sector ISO image. It keeps only the Form 1 data of each "
		               "sector, so the XA audio and the video (Form 2 sectors) are lost. Make a raw "
		               "dump of the disc instead (.bin + .cue, 2352-byte sectors)",
		            bin);
	}
	if (size == 0 || size % RAW != 0) {
		return fail(c, "%s: size %lld is not a multiple of %u: not a raw 2352-byte-sector image, "
		               "or truncated",
		            bin, (long long)size, RAW);
	}
	if (size / RAW > MAX_SECTORS) {
		return fail(c, "%s: %lld sectors, too large for a CD image", bin, (long long)(size / RAW));
	}
	c->img_sectors = (uint32_t)(size / RAW);
	c->next = UINT32_MAX;
	return 0;
}

static uint8_t bcd(uint32_t v)
{
	return (uint8_t)(v / 10 << 4 | v % 10);
}

/* One raw sector, checked: sync pattern, header address = lba, Mode 2. */
static int read_raw(Ctx *c, uint32_t lba, uint8_t s[RAW])
{
	static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
	uint32_t a = lba + 150;

	if (lba >= c->img_sectors) {
		return fail(c, "sector %u is past the end of the image (%u sectors): truncated image", lba,
		            c->img_sectors);
	}
	if ((lba != c->next && fseeko(c->img, (off_t)lba * RAW, SEEK_SET) != 0) ||
	    fread(s, 1, RAW, c->img) != RAW) {
		c->next = UINT32_MAX;
		return fail(c, "cannot read sector %u of the image", lba);
	}
	c->next = lba + 1;
	if (memcmp(s, sync, 12) != 0 || s[12] != bcd(a / 4500) || s[13] != bcd(a / 75 % 60) ||
	    s[14] != bcd(a % 75) || s[15] != 2) {
		return fail(c, "sector %u: bad sync or header (not a raw Mode 2 image, or damaged)", lba);
	}
	return 0;
}

/* ---- ISO9660 ---- */

/* Adds the files of a directory extent (and of its subdirectories) to l, with their ISO size. */
static int iso_dir(Ctx *c, List *l, uint32_t lba, uint32_t size, const char *prefix, int depth)
{
	uint8_t s[RAW];
	uint32_t i;

	if (depth > MAX_DEPTH || size > (1u << 20)) {
		return fail(c, "ISO9660 directory %s: too deep or too large (damaged image)", prefix);
	}
	for (i = 0; i < (size + DATA - 1) / DATA; i++) {
		const uint8_t *d = s + 24;
		uint32_t pos = 0;

		if (read_raw(c, lba + i, s) != 0) {
			return -1;
		}
		/* Records do not cross sectors: a zero length ends the sector. */
		while (pos < DATA && d[pos] != 0) {
			const uint8_t *r = d + pos;
			Entry e = { 0 };

			if (r[0] < 34 || pos + r[0] > DATA || 33u + r[32] > r[0]) {
				return fail(c, "ISO9660 directory %s: bad record (damaged image)", prefix);
			}
			pos += r[0];
			if (r[32] == 1 && r[33] <= 1) {
				continue; /* "." and ".." */
			}
			if (snprintf(e.path, sizeof(e.path), "%s\\%.*s", prefix, r[32], (const char *)r + 33) >=
			    (int)sizeof(e.path)) {
				return fail(c, "%s\\%.*s: path longer than %u bytes", prefix, r[32], (const char *)r + 33,
				            PACK_PATH_SIZE - 1);
			}
			e.lba = rd32(r + 2);
			e.size = rd32(r + 10);
			e.sectors = (uint32_t)(((uint64_t)e.size + DATA - 1) / DATA);
			if (r[25] & 2) {
				if (iso_dir(c, l, e.lba, e.size, e.path, depth + 1) != 0) {
					return -1;
				}
			} else if (list_add(c, l, &e) != 0) {
				return -1;
			}
		}
	}
	return 0;
}

/* Every file of the image's ISO9660 tree, sorted by LBA and path; size is the ISO size. */
static int iso_files(Ctx *c, List *l)
{
	uint8_t s[RAW];

	if (read_raw(c, 16, s) != 0) {
		return -1;
	}
	if (s[24] != 1 || memcmp(s + 25, "CD001", 5) != 0) {
		return fail(c, "no ISO9660 volume descriptor at sector 16 (not a PlayStation disc)");
	}
	if (iso_dir(c, l, rd32(s + 24 + 156 + 2), rd32(s + 24 + 156 + 10), "", 0) != 0) {
		return -1;
	}
	qsort(l->e, l->n, sizeof(*l->e), entry_cmp);
	return 0;
}

/*
 * Reads the sectors of e (whose size is its ISO size) and sets its mode, size and SHA-256 as the
 * Manifest defines them. With out, also writes the bytes stored for out_mode.
 */
static int scan(Ctx *c, Entry *e, FILE *out, uint32_t out_mode)
{
	uint8_t s[RAW];
	Sha256 h1, h2;
	uint32_t left = e->size, i;
	int form2 = 0;

	Sha256_Init(&h1);
	Sha256_Init(&h2);
	for (i = 0; i < e->sectors; i++) {
		uint32_t n = left < DATA ? left : DATA;

		if (read_raw(c, e->lba + i, s) != 0) {
			return -1;
		}
		form2 |= s[18] & 0x20; /* submode: Form 2 */
		Sha256_Update(&h1, s + 24, n);
		Sha256_Update(&h2, s + 16, PACK_F2_SECTOR);
		left -= n;
		if (out != NULL && (out_mode == PACK_F2 ? fwrite(s + 16, 1, PACK_F2_SECTOR, out) != PACK_F2_SECTOR
		                                        : fwrite(s + 24, 1, n, out) != n)) {
			return fail(c, "cannot write the Pack (disk full?)");
		}
	}
	if (form2) {
		if (e->size != e->sectors * DATA) {
			return fail(c, "%s: has Form 2 sectors but its ISO size %u is not %u sectors * 2048", e->path,
			            e->size, e->sectors);
		}
		e->mode = PACK_F2;
		e->size = e->sectors * PACK_F2_SECTOR;
		Sha256_Final(&h2, e->sha);
	} else {
		e->mode = PACK_F1;
		Sha256_Final(&h1, e->sha);
	}
	return 0;
}

/* ---- Manifest ---- */

static int parse_u32(const char *s, uint32_t *v)
{
	char *end;
	unsigned long long x;

	if (*s < '0' || *s > '9' || strlen(s) > 10) {
		return -1;
	}
	x = strtoull(s, &end, 10);
	if (*end != 0 || x > UINT32_MAX) {
		return -1;
	}
	*v = (uint32_t)x;
	return 0;
}

static int parse_sha(const char *s, uint8_t sha[32])
{
	int i;

	if (strlen(s) != 64 || strspn(s, "0123456789abcdef") != 64) {
		return -1;
	}
	for (i = 0; i < 32; i++) {
		sha[i] = (uint8_t)strtoul((char[3]){ s[i * 2], s[i * 2 + 1], 0 }, NULL, 16);
	}
	return 0;
}

/* The fields of a row (or index entry) agree: the sector count follows from the size, as the CD
 * model (port/backend/cdrom.c) derives it. */
static int entry_valid(const Entry *e)
{
	if (e->path[0] == 0 || (uint64_t)e->lba + e->sectors > MAX_SECTORS) {
		return 0;
	}
	return e->mode == PACK_F1 ? e->sectors == (uint32_t)(((uint64_t)e->size + DATA - 1) / DATA)
	                          : e->mode == PACK_F2 && e->size == e->sectors * PACK_F2_SECTOR;
}

static int parse_row(char *line, Entry *e)
{
	char *f[6];
	int i;

	for (i = 0; i < 6; i++) {
		f[i] = line;
		line += strcspn(line, "\t");
		if ((*line == 0) != (i == 5)) {
			return -1;
		}
		*line++ = 0;
	}
	if (*f[0] == 0 || strlen(f[0]) >= PACK_PATH_SIZE || parse_u32(f[1], &e->lba) != 0 ||
	    parse_u32(f[2], &e->sectors) != 0 || parse_u32(f[4], &e->size) != 0 || parse_sha(f[5], e->sha) != 0) {
		return -1;
	}
	if (strcmp(f[3], "F1") == 0) {
		e->mode = PACK_F1;
	} else if (strcmp(f[3], "F2") == 0) {
		e->mode = PACK_F2;
	} else {
		return -1;
	}
	strcpy(e->path, f[0]);
	return entry_valid(e) ? 0 : -1;
}

/* Reads a Manifest: the @exe row, the file rows (sorted by LBA and path) and its SHA-256. */
static int manifest_read(Ctx *c, const char *tsv, Entry *exe, List *l, uint8_t sha[32])
{
	size_t size;
	char *text = read_text(c, tsv, &size);
	char *p = text;
	int line = 0, have_exe = 0, r = -1;
	Sha256 h;

	if (text == NULL) {
		return -1;
	}
	Sha256_Init(&h);
	Sha256_Update(&h, text, size);
	Sha256_Final(&h, sha);
	while (*p != 0) {
		char *nl = p + strcspn(p, "\n");
		Entry e = { 0 };

		line++;
		if (*nl == 0) {
			fail(c, "%s line %d: no newline at the end", tsv, line);
			goto out;
		}
		*nl = 0;
		if (*p != '#' && *p != 0) {
			if (parse_row(p, &e) != 0) {
				fail(c, "%s line %d: bad row (want path, lba, sectors, F1|F2, size, sha256)", tsv, line);
				goto out;
			}
			if (!have_exe) {
				if (strcmp(e.path, "@exe") != 0) {
					fail(c, "%s line %d: the first row must be @exe", tsv, line);
					goto out;
				}
				*exe = e;
				have_exe = 1;
			} else if (l->n > 0 && !follows(&l->e[l->n - 1], &e)) {
				fail(c, "%s line %d: %s is not sorted by LBA and path, or shares sectors with the "
				        "previous file", tsv, line, e.path);
				goto out;
			} else if (list_add(c, l, &e) != 0) {
				goto out;
			}
		}
		p = nl + 1;
	}
	r = !have_exe ? fail(c, "%s: no @exe row", tsv) : l->n == 0 ? fail(c, "%s: no file rows", tsv) : 0;
out:
	free(text);
	return r;
}

static void sha_hex(const uint8_t sha[32], char out[65])
{
	int i;

	for (i = 0; i < 32; i++) {
		snprintf(out + i * 2, 3, "%02x", sha[i]);
	}
}

static void write_row(FILE *f, const char *path, const Entry *e)
{
	char hex[65];

	sha_hex(e->sha, hex);
	fprintf(f, "%s\t%u\t%u\t%s\t%u\t%s\n", path, e->lba, e->sectors, e->mode == PACK_F2 ? "F2" : "F1",
	        e->size, hex);
}

int pack_manifest(const char *image, const char *tsv, char *err, size_t errlen)
{
	Ctx c = { err, errlen, NULL, 0, 0 };
	List l = { 0 };
	const Entry *exe = NULL;
	FILE *f = NULL;
	size_t i;
	int r = -1;

	if (image_open(&c, image) != 0 || iso_files(&c, &l) != 0) {
		goto out;
	}
	for (i = 0; i < l.n; i++) {
		if (scan(&c, &l.e[i], NULL, 0) != 0) {
			goto out;
		}
		if (strcmp(l.e[i].path, PACK_EXE_PATH) == 0) {
			exe = &l.e[i];
		}
	}
	if (exe == NULL) {
		fail(&c, "only the USA version of Digimon World (SLUS-01032) is supported: %s is not on this disc",
		     PACK_EXE_PATH);
		goto out;
	}
	f = fopen(tsv, "wb");
	if (f == NULL) {
		fail(&c, "cannot create %s", tsv);
		goto out;
	}
	fprintf(f, "# Digimon World USA (SLUS-01032) disc Manifest, written by dw1-pack manifest.\n"
	           "# No disc bytes: path, lba, sectors, mode (F1/F2), size, sha256. See port/host/pack/pack.h.\n");
	write_row(f, "@exe", exe);
	for (i = 0; i < l.n; i++) {
		write_row(f, l.e[i].path, &l.e[i]);
	}
	r = 0;
out:
	if (f != NULL && (ferror(f) | fclose(f)) != 0 && r == 0) {
		r = fail(&c, "cannot write %s", tsv);
	}
	if (c.img != NULL) {
		fclose(c.img);
	}
	free(l.e);
	return r;
}

/* ---- Pack ---- */

/* Offsets of every entry, in Manifest (LBA) order; the data offset and the end of the Pack. */
static int layout(Ctx *c, List *m, uint32_t *data_offset, uint32_t *end)
{
	uint64_t pos = align(PACK_HEADER_SIZE + (uint64_t)PACK_ENTRY_SIZE * m->n);
	size_t i;

	*data_offset = (uint32_t)pos;
	for (i = 0; i < m->n; i++) {
		m->e[i].offset = align(pos);
		pos = (uint64_t)m->e[i].offset + m->e[i].size;
		if (pos > UINT32_MAX - PACK_ALIGN) {
			return fail(c, "the Pack would be larger than 4 GB");
		}
	}
	*end = (uint32_t)pos;
	return 0;
}

static void header_encode(uint8_t h[PACK_HEADER_SIZE], uint32_t count, const uint8_t msha[32], uint32_t data_offset)
{
	memset(h, 0, PACK_HEADER_SIZE);
	memcpy(h, PACK_MAGIC, sizeof(PACK_MAGIC));
	wr32(h + 8, PACK_FORMAT_VERSION);
	wr32(h + 12, count);
	memcpy(h + 16, msha, 32);
	wr32(h + 48, PACK_HEADER_SIZE);
	wr32(h + 52, data_offset);
}

static void entry_encode(uint8_t b[PACK_ENTRY_SIZE], const Entry *e)
{
	memset(b, 0, PACK_ENTRY_SIZE);
	memcpy(b, e->path, strlen(e->path));
	wr32(b + 64, e->lba);
	wr32(b + 68, e->sectors);
	wr32(b + 72, e->mode);
	wr32(b + 76, e->size);
	wr32(b + 80, e->offset);
	memcpy(b + 84, e->sha, 32);
}

static int write_zeros(Ctx *c, FILE *f, uint64_t *pos, uint32_t to)
{
	for (; *pos < to; (*pos)++) {
		if (fputc(0, f) == EOF) {
			return fail(c, "cannot write the Pack (disk full?)");
		}
	}
	return 0;
}

static int same(const Entry *a, const Entry *b)
{
	return a->sectors == b->sectors && a->mode == b->mode && a->size == b->size && memcmp(a->sha, b->sha, 32) == 0;
}

/* The exe, read by its LBA alone: identifies the disc before anything else is read. */
static int check_exe(Ctx *c, const Entry *exe)
{
	Entry d = *exe;
	int bad;

	strcpy(d.path, PACK_EXE_PATH);
	d.size = exe->mode == PACK_F2 ? exe->sectors * DATA : exe->size;
	bad = scan(c, &d, NULL, 0) != 0;

	if (bad || !same(&d, exe)) {
		char why[256];

		snprintf(why, sizeof(why), "%s", bad ? c->err : "its mode, size or SHA-256 differs");
		return fail(c, "only the USA version of Digimon World (SLUS-01032) is supported: %s (sectors %u..%u) "
		               "is not the retail USA exe (%s)",
		            PACK_EXE_PATH, exe->lba, exe->lba + exe->sectors, why);
	}
	return 0;
}

int pack_build(const char *image, const char *tsv, const char *pack, char *err, size_t errlen)
{
	Ctx c = { err, errlen, NULL, 0, 0 };
	List m = { 0 }, d = { 0 };
	Entry exe;
	uint8_t msha[32], b[PACK_HEADER_SIZE > PACK_ENTRY_SIZE ? PACK_HEADER_SIZE : PACK_ENTRY_SIZE];
	char tmp[1100];
	FILE *f = NULL;
	uint32_t data_offset, end;
	uint64_t pos = 0;
	size_t i, j;
	int r = -1;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", pack) >= (int)sizeof(tmp)) {
		return fail(&c, "the Pack path is too long");
	}
	if (manifest_read(&c, tsv, &exe, &m, msha) != 0 || layout(&c, &m, &data_offset, &end) != 0 ||
	    image_open(&c, image) != 0 || check_exe(&c, &exe) != 0 || iso_files(&c, &d) != 0) {
		goto out;
	}
	/* Same files at the same place: both lists are sorted by LBA and path. */
	for (i = j = 0; i < m.n || j < d.n;) {
		int k = i == m.n ? 1 : j == d.n ? -1 : entry_cmp(&m.e[i], &d.e[j]);

		if (k < 0) {
			fail(&c, "%s: in the Manifest but not on the disc at sector %u (modified or bad dump)", m.e[i].path,
			     m.e[i].lba);
			goto out;
		}
		if (k > 0) {
			fail(&c, "%s: on the disc at sector %u but not in the Manifest (modified or bad dump)", d.e[j].path,
			     d.e[j].lba);
			goto out;
		}
		if (m.e[i].sectors != d.e[j].sectors) {
			fail(&c, "%s: %u sectors on the disc, %u in the Manifest (modified or bad dump)", m.e[i].path,
			     d.e[j].sectors, m.e[i].sectors);
			goto out;
		}
		i++, j++;
	}
	f = fopen(tmp, "wb");
	if (f == NULL) {
		fail(&c, "cannot create %s", tmp);
		goto out;
	}
	header_encode(b, (uint32_t)m.n, msha, data_offset);
	if (fwrite(b, 1, PACK_HEADER_SIZE, f) != PACK_HEADER_SIZE) {
		fail(&c, "cannot write the Pack (disk full?)");
		goto out;
	}
	pos = PACK_HEADER_SIZE;
	for (i = 0; i < m.n; i++, pos += PACK_ENTRY_SIZE) {
		entry_encode(b, &m.e[i]);
		if (fwrite(b, 1, PACK_ENTRY_SIZE, f) != PACK_ENTRY_SIZE) {
			fail(&c, "cannot write the Pack (disk full?)");
			goto out;
		}
	}
	for (i = 0; i < m.n; i++) {
		if (write_zeros(&c, f, &pos, m.e[i].offset) != 0 || scan(&c, &d.e[i], f, m.e[i].mode) != 0) {
			goto out;
		}
		if (!same(&d.e[i], &m.e[i])) {
			fail(&c, "%s: differs from the Manifest (mode, size or SHA-256): modified or bad dump",
			     m.e[i].path);
			goto out;
		}
		pos += m.e[i].size;
	}
	PORT_ASSERT(pos == end);
	if (fclose(f) != 0) {
		f = NULL;
		fail(&c, "cannot write %s (disk full?)", tmp);
		goto out;
	}
	f = NULL;
	if (rename(tmp, pack) != 0) {
		fail(&c, "cannot rename %s to %s", tmp, pack);
		goto out;
	}
	r = 0;
out:
	if (f != NULL) {
		fclose(f);
	}
	if (r != 0) {
		remove(tmp);
	}
	if (c.img != NULL) {
		fclose(c.img);
	}
	free(m.e);
	free(d.e);
	return r;
}

int pack_verify(const char *pack, const char *tsv, char *err, size_t errlen)
{
	Ctx c = { err, errlen, NULL, 0, 0 };
	List m = { 0 };
	Entry exe;
	uint8_t msha[32], want[PACK_HEADER_SIZE + PACK_ENTRY_SIZE], got[sizeof(want)];
	static uint8_t buf[1 << 16]; /* off the stack: Emscripten's default stack is 64 KiB */
	FILE *f;
	uint32_t data_offset, end;
	uint64_t pos;
	size_t i;
	int r = -1;

	if (manifest_read(&c, tsv, &exe, &m, msha) != 0 || layout(&c, &m, &data_offset, &end) != 0) {
		free(m.e);
		return -1;
	}
	f = fopen(pack, "rb");
	if (f == NULL) {
		fail(&c, "cannot open %s", pack);
		goto out;
	}
	header_encode(want, (uint32_t)m.n, msha, data_offset);
	if (fread(got, 1, PACK_HEADER_SIZE, f) != PACK_HEADER_SIZE || memcmp(got, want, PACK_HEADER_SIZE) != 0) {
		fail(&c, "%s: bad header (not a Pack, another format version, or built from another Manifest)", pack);
		goto out;
	}
	for (i = 0; i < m.n; i++) {
		entry_encode(want, &m.e[i]);
		if (fread(got, 1, PACK_ENTRY_SIZE, f) != PACK_ENTRY_SIZE || memcmp(got, want, PACK_ENTRY_SIZE) != 0) {
			fail(&c, "%s: its index entry in %s differs from the Manifest (corrupt Pack)", m.e[i].path, pack);
			goto out;
		}
	}
	pos = PACK_HEADER_SIZE + (uint64_t)PACK_ENTRY_SIZE * m.n;
	for (i = 0; i < m.n; i++) {
		uint32_t left = m.e[i].size;
		uint8_t sha[32];
		Sha256 h;
		int c0;

		for (; pos < m.e[i].offset; pos++) {
			if ((c0 = fgetc(f)) != 0) {
				fail(&c, "%s: %s before its data in %s (corrupt Pack)", m.e[i].path,
				     c0 == EOF ? "truncated" : "nonzero padding", pack);
				goto out;
			}
		}
		Sha256_Init(&h);
		while (left > 0) {
			size_t n = left < sizeof(buf) ? left : sizeof(buf);

			if (fread(buf, 1, n, f) != n) {
				fail(&c, "%s: truncated in %s (corrupt Pack)", m.e[i].path, pack);
				goto out;
			}
			Sha256_Update(&h, buf, n);
			left -= (uint32_t)n;
		}
		Sha256_Final(&h, sha);
		if (memcmp(sha, m.e[i].sha, 32) != 0) {
			fail(&c, "%s: SHA-256 differs from the Manifest in %s (corrupt Pack)", m.e[i].path, pack);
			goto out;
		}
		pos += m.e[i].size;
	}
	PORT_ASSERT(pos == end);
	if (fgetc(f) != EOF) {
		fail(&c, "%s: bytes after the last file (corrupt Pack)", pack);
		goto out;
	}
	r = 0;
out:
	if (f != NULL) {
		fclose(f);
	}
	free(m.e);
	return r;
}

/* ---- reader ---- */

struct Pack {
	FILE *f;
	List l;
};

static void entry_decode(const uint8_t b[PACK_ENTRY_SIZE], Entry *e)
{
	memset(e, 0, sizeof(*e));
	memcpy(e->path, b, PACK_PATH_SIZE - 1);
	e->lba = rd32(b + 64);
	e->sectors = rd32(b + 68);
	e->mode = rd32(b + 72);
	e->size = rd32(b + 76);
	e->offset = rd32(b + 80);
	memcpy(e->sha, b + 84, 32);
}

Pack *pack_open(const char *pack, const char *manifest_sha256, char *err, size_t errlen)
{
	Ctx c = { err, errlen, NULL, 0, 0 };
	Pack *p = calloc(1, sizeof(*p));
	uint8_t h[PACK_HEADER_SIZE], want[PACK_HEADER_SIZE > PACK_ENTRY_SIZE ? PACK_HEADER_SIZE : PACK_ENTRY_SIZE];
	uint8_t b[PACK_ENTRY_SIZE];
	char hex[65];
	uint32_t count, data_offset, i;
	uint64_t pos;

	if (p == NULL) {
		fail(&c, "out of memory");
		return NULL;
	}
	p->f = fopen(pack, "rb");
	if (p->f == NULL) {
		fail(&c, "cannot open %s", pack);
		goto bad;
	}
	if (fread(h, 1, PACK_HEADER_SIZE, p->f) != PACK_HEADER_SIZE || memcmp(h, PACK_MAGIC, sizeof(PACK_MAGIC)) != 0 ||
	    rd32(h + 8) != PACK_FORMAT_VERSION) {
		fail(&c, "%s is not a Pack of format version %u: build it from your disc with dw1-pack build", pack,
		     PACK_FORMAT_VERSION);
		goto bad;
	}
	sha_hex(h + 16, hex);
	if (manifest_sha256 != NULL && strcmp(hex, manifest_sha256) != 0) {
		fail(&c, "%s was built from another Manifest (SHA-256 %s; this build expects %s): build it again "
		         "from your disc with this version's dw1-pack build",
		     pack, hex, manifest_sha256);
		goto bad;
	}
	/* Header and index exactly as pack_build writes them: canonical entries, sorted, laid out. */
	count = rd32(h + 12);
	data_offset = align(PACK_HEADER_SIZE + (uint64_t)PACK_ENTRY_SIZE * count);
	header_encode(want, count, h + 16, data_offset);
	if (count == 0 || count > MAX_SECTORS || memcmp(h, want, PACK_HEADER_SIZE) != 0) {
		goto corrupt;
	}
	pos = data_offset;
	for (i = 0; i < count; i++) {
		Entry e;

		if (fread(b, 1, PACK_ENTRY_SIZE, p->f) != PACK_ENTRY_SIZE) {
			goto corrupt;
		}
		entry_decode(b, &e);
		entry_encode(want, &e);
		if (memcmp(b, want, PACK_ENTRY_SIZE) != 0 || !entry_valid(&e) || e.offset != align(pos) ||
		    (p->l.n > 0 && !follows(&p->l.e[p->l.n - 1], &e))) {
			goto corrupt;
		}
		pos = (uint64_t)e.offset + e.size;
		if (list_add(&c, &p->l, &e) != 0) {
			goto bad;
		}
	}
	if (fseeko(p->f, 0, SEEK_END) != 0 || ftello(p->f) != (off_t)pos) {
		goto corrupt;
	}
	return p;
corrupt:
	fail(&c, "%s: bad header or index (corrupt or truncated Pack): build it again with dw1-pack build", pack);
bad:
	pack_close(p);
	return NULL;
}

void pack_close(Pack *p)
{
	if (p != NULL) {
		if (p->f != NULL) {
			fclose(p->f);
		}
		free(p->l.e);
		free(p);
	}
}

const PackEntry *pack_find_path(const Pack *p, const char *path)
{
	size_t i;

	for (i = 0; i < p->l.n; i++) {
		if (strcmp(p->l.e[i].path, path) == 0) {
			return &p->l.e[i];
		}
	}
	return NULL;
}

/* ponytail: linear scans over the ~820 files; an index by LBA if a profile ever shows them. */
const PackEntry *pack_find_lba(const Pack *p, uint32_t lba)
{
	size_t i;

	for (i = 0; i < p->l.n; i++) {
		if (lba - p->l.e[i].lba < p->l.e[i].sectors) {
			return &p->l.e[i];
		}
	}
	return NULL;
}

int pack_read(Pack *p, const PackEntry *e, uint32_t offset, void *buf, uint32_t n)
{
	if (offset > e->size || n > e->size - offset) {
		return -1;
	}
	return fseeko(p->f, (off_t)e->offset + offset, SEEK_SET) == 0 && fread(buf, 1, n, p->f) == n ? 0 : -1;
}
