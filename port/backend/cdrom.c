/* CD model over the Pack (see cdrom.h). */
#include "cdrom.h"

#include <string.h>

#include "../psyq/port.h"

#define SUBHEADER 8u

static Pack *pack;

int cdrom_open(const char *path, const char *manifest_sha256, char *err, size_t errlen)
{
	cdrom_close();
	pack = pack_open(path, manifest_sha256, err, errlen);
	return pack != NULL ? 0 : -1;
}

void cdrom_close(void)
{
	pack_close(pack);
	pack = NULL;
}

const PackEntry *cdrom_find_path(const char *path)
{
	return pack != NULL ? pack_find_path(pack, path) : NULL;
}

const PackEntry *cdrom_find_lba(uint32_t lba)
{
	return pack != NULL ? pack_find_lba(pack, lba) : NULL;
}

int cdrom_read_file(const PackEntry *entry, uint32_t offset, void *out, uint32_t size)
{
	return pack && entry ? pack_read(pack, entry, offset, out, size) : -1;
}

static uint8_t bcd(uint32_t v)
{
	return (uint8_t)(v / 10 << 4 | v % 10);
}

uint32_t cdrom_read_sector(uint32_t lba, uint8_t *out, uint32_t size)
{
	PORT_ASSERT(size == CDROM_SECTOR_DATA || size == CDROM_SECTOR_SIZE1);
	memset(out, 0, size);
	if (pack == NULL)
		return 0; /* empty disc */
	const PackEntry *e = pack_find_lba(pack, lba);
	PORT_ASSERT(e != NULL); /* the game reads only files of the index */
	if (e == NULL)
		return 0;
	uint32_t k = lba - e->lba, n = size;
	int ok;
	if (e->mode == PACK_F1) {
		/* The game reads Form 1 files with 2048-byte sectors only: in src/, only the .STR
		 * movies (Form 2) are read with CdlModeSize1. */
		PORT_ASSERT(size == CDROM_SECTOR_DATA);
		if (size != CDROM_SECTOR_DATA)
			return 0;
		uint32_t off = k * CDROM_SECTOR_DATA;
		n = e->size - off < CDROM_SECTOR_DATA ? e->size - off : CDROM_SECTOR_DATA;
		ok = pack_read(pack, e, off, out, n) == 0;
	} else if (size == CDROM_SECTOR_DATA) {
		ok = pack_read(pack, e, k * PACK_F2_SECTOR + SUBHEADER, out, CDROM_SECTOR_DATA) == 0;
	} else {
		uint32_t a = lba + 150;
		out[0] = bcd(a / 4500), out[1] = bcd(a / 75 % 60), out[2] = bcd(a % 75), out[3] = 2;
		ok = pack_read(pack, e, k * PACK_F2_SECTOR, out + 4, PACK_F2_SECTOR) == 0;
	}
	/* The Pack was checked when opened: a failed read means it changed or the disk failed. */
	if (!ok) {
		fprintf(stderr, "cdrom: cannot read sector %u of %s from the Pack\n", lba, e->path);
		abort();
	}
	return n;
}
