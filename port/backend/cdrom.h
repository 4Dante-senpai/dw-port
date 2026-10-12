#ifndef PORT_BACKEND_CDROM_H
#define PORT_BACKEND_CDROM_H

/*
 * CD model over the Pack (AD-12): the disc the game sees, as data sectors by LBA. No timing (libcd
 * owns it over the virtual clock) and no read errors. Without an open Pack the disc is empty:
 * every sector reads as zeros and no file is found.
 */

#include <stddef.h>
#include <stdint.h>

#include "../host/pack/pack.h"

#define CDROM_SECTOR_DATA 2048u  /* CdlModeSize1 off: user data */
#define CDROM_SECTOR_SIZE1 2340u /* CdlModeSize1: header + subheader + data + EDC/ECC */

/* Opens the Pack (see pack_open); 0 on success, -1 with a message in err. */
int cdrom_open(const char *pack, const char *manifest_sha256, char *err, size_t errlen);
void cdrom_close(void);

/* The file at that exact path (\DIR\NAME;1), or the file holding lba; NULL if none. */
const PackEntry *cdrom_find_path(const char *path);
const PackEntry *cdrom_find_lba(uint32_t lba);

/* One sector as the drive delivers it, size CDROM_SECTOR_DATA or CDROM_SECTOR_SIZE1:
 * - Form 1 file: its user data (zeros past the file size); not readable with CdlModeSize1.
 * - Form 2 file: the 2048 bytes after the subheader, or the rebuilt header (BCD MSF, mode 2)
 *   followed by the 2336 stored bytes.
 * An LBA outside every file of the index is a PORT_ASSERT (zeros in release builds).
 * Returns how many leading bytes of out are file data: size, less in the last sector of a Form 1
 * file (its slack is not in the Pack), 0 on the empty disc or outside the index. */
uint32_t cdrom_read_sector(uint32_t lba, uint8_t *out, uint32_t size);

#endif
