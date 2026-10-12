#ifndef PORT_PACK_PACK_H
#define PORT_PACK_PACK_H

/*
 * The Pack: the files of the user's disc, checked against the versioned Manifest
 * (tools/port/manifest/us.tsv) and stored in one file the game reads instead of the CD.
 *
 * Manifest (TSV): '#' comment lines, then the row "@exe" (the fields of \SLUS_010.32;1, checked
 * first by LBA to identify the disc), then one row per ISO9660 file sorted by LBA and path:
 *   path  lba  sectors  mode  size  sha256
 * path as the game asks for it (\MOVIE\OP1.STR;1); mode F1 (Form 1: size = ISO size, the 2048-byte
 * user data of each sector) or F2 (some sector has the Form 2 submode bit: size = sectors * 2336,
 * subheader + data of each sector; its ISO size must be sectors * 2048); sha256 of those bytes.
 *
 * Pack layout (little-endian):
 *   header, 64 bytes:  magic "DW1PACK\0", u32 format_version, u32 count, u8 manifest_sha256[32],
 *                      u32 index_offset, u32 data_offset, 8 reserved zero bytes
 *   index, count entries of 128 bytes: char path[64] (NUL-padded), u32 lba, u32 sectors,
 *                      u32 mode (PACK_F1/PACK_F2), u32 size, u32 offset, u8 sha256[32],
 *                      12 reserved zero bytes
 *   data:              each entry's size bytes at its offset, offsets aligned to 2048, in LBA
 *                      order, zero padding between them. The slack after the last Form 1 sector
 *                      is not stored [hypothesis: the game never reads it; checked in story 2.8].
 */

#include <stddef.h>
#include <stdint.h>

#define PACK_MAGIC "DW1PACK"
#define PACK_FORMAT_VERSION 1u
#define PACK_HEADER_SIZE 64u
#define PACK_ENTRY_SIZE 128u
#define PACK_PATH_SIZE 64u
#define PACK_ALIGN 2048u
#define PACK_F1 1u
#define PACK_F2 2u
#define PACK_F2_SECTOR 2336u /* raw sector minus sync and header: subheader + data */
#define PACK_EXE_PATH "\\SLUS_010.32;1"

/* All return 0 on success, -1 with a one-paragraph message in err on any failure. */

/* Writes the Manifest of a disc image (.cue or raw 2352-byte .bin) to tsv. */
int pack_manifest(const char *image, const char *tsv, char *err, size_t errlen);
/* Builds the Pack of a disc image if every file matches the Manifest. It is written to pack.tmp
 * and renamed only after everything checked: a failed build creates nothing and leaves an
 * existing file at pack untouched. */
int pack_build(const char *image, const char *tsv, const char *pack, char *err, size_t errlen);
/* Checks a Pack against the Manifest: header, index and the SHA-256 of every file. */
int pack_verify(const char *pack, const char *tsv, char *err, size_t errlen);

#endif
