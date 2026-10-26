/* CD model over the Pack (see cdrom.h). */
#include "cdrom.h"

#include <string.h>

#include "../psyq/port.h"

#define SUBHEADER 8u

static Pack *pack;
#include "cd_geometry.h"
static int usa_geometry;

int cdrom_open(const char *path, const char *manifest_sha256, char *err, size_t errlen)
{
	cdrom_close();
	pack = pack_open(path, manifest_sha256, err, errlen);
	usa_geometry = pack && manifest_sha256 && !strcmp(manifest_sha256,
        "2750890c75ab70d3d0e134ccc4569d2737ff0e4769fc2cf3d87a8a2e745b20d3");
	return pack != NULL ? 0 : -1;
}

void cdrom_close(void)
{
	pack_close(pack);
	pack = NULL;
    usa_geometry = 0;
}

uint32_t cdrom_directory_lba(const char *path)
{
    if (!usa_geometry) return 0;
    const char *base = strrchr(path, '\\');
    size_t length = base ? (size_t)(base-path) : 0;
    for (unsigned i=0; i<sizeof(iso_directories)/sizeof(iso_directories[0]); i++)
        if (strlen(iso_directories[i].path)==length &&
            !strncmp(path, iso_directories[i].path, length)) return iso_directories[i].lba;
    return 0;
}

uint32_t cdrom_path_table_lba(void) { return usa_geometry ? ISO_PATH_TABLE_LBA : 0; }

void cdrom_iso_copy_counts(uint32_t lba, unsigned *copies, unsigned *bytes)
{
    *copies = *bytes = 0;
    if (!usa_geometry) return;
    if (lba == ISO_PATH_TABLE_LBA) {
        *copies = ISO_PATH_TABLE_ENTRIES; *bytes = ISO_PATH_TABLE_NAME_BYTES;
        return;
    }
    for (unsigned i=0; i<sizeof(iso_directories)/sizeof(iso_directories[0]); i++)
        if (iso_directories[i].lba == lba) {
            *copies = iso_directories[i].entries - 2;
            *bytes = iso_directories[i].name_bytes;
            return;
        }
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

uint32_t cdrom_stream_sector(uint32_t lba, uint8_t out[CDROM_SECTOR_SIZE1])
{
    const PackEntry *entry=cdrom_find_lba(lba);
    if (!entry || entry->mode==PACK_F2) return cdrom_read_sector(lba,out,CDROM_SECTOR_SIZE1);
    memset(out,0,CDROM_SECTOR_SIZE1);
    uint32_t a=lba+150;
    out[0]=bcd(a/4500); out[1]=bcd(a/75%60); out[2]=bcd(a%75); out[3]=2;
    out[6]=out[10]=8; /* Form 1 data, never XA. */
    return cdrom_read_sector(lba,out+12,CDROM_SECTOR_DATA);
}
