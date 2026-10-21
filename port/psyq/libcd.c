__attribute__((weak)) void port_scene_movie_xa(void);
__attribute__((weak)) void port_scene_movie_timing(const char *event, unsigned value);
/* libcd: commands, data reads and file search over the CD model (port/backend/cdrom.h) and the
 * virtual clock (AD-8). asm:asm/us/main/psyq/libcd.s (mw_lib41). Not modeled: CD-DA, read errors and retries, the shell-open status. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../backend/cdrom.h"
#include "../host/pack/sha256.h"
#include "clock.h"
#include "overlay.h"
#include "libcd_stream.h"
#include "../backend/xa.h"
#include "../backend/spu.h"
#include "port.h"
#include "ps1mem.h"
#include "psyq.h"

/* [hypothesis] seek before the first sector of a read (and of a file search); the seek and the
 * sector times are calibrated against the reference in story 3.10. */
#define CDROM_SEEK_CYCLES (CLOCK_HZ / 10)
#define SECTOR_CYCLES_1X (CLOCK_HZ / 75)
#define SECTOR_CYCLES_2X (CLOCK_HZ / 150)

_Static_assert(sizeof(CdlFILE) == 0x18 && offsetof(CdlFILE, size) == 4 && offsetof(CdlFILE, name) == 8,
               "CdlFILE as in libcd.h (CdSearchFile copies 0x18 bytes)");

/* Runtime hook after an overlay load (crt.c, --until-overlay); absent in unit tests. */
__attribute__((weak)) void port_overlay_done(void);

static CdlLOC setloc;
static unsigned char drive_mode;
static struct { uint32_t lba, end; uint64_t period; int event, active; } rt;
static CdlCB read_callback;
static unsigned char read_result[8]; /* status bytes passed to the read callback */

/* The read in progress (asm: ReadAttr). */
static struct {
	uint8_t *buf;
	uint32_t lba;   /* first sector */
	uint32_t count; /* sectors asked */
	uint32_t done;  /* sectors delivered */
	uint32_t size;  /* bytes per sector */
	uint64_t sector_cycles;
	int event;      /* pending sector event, 0 if none */
	int aborted;
} rd;

static int from_bcd(unsigned char b)
{
	return b / 16 * 10 + b % 16;
}

static unsigned char to_bcd(int i)
{
	return (unsigned char)(i / 10 * 16 + i % 10);
}

/* asm: CdPosToInt -- BCD minute:second:sector to an LBA (2 s of lead-in = 150 sectors). */
static int pos_to_int(const CdlLOC *p)
{
	return (from_bcd(p->minute) * 60 + from_bcd(p->second)) * 75 + from_bcd(p->sector) - 150;
}

static void xa_input(int16_t *l, int16_t *r, void *arg) { (void)arg; XaDec_Pop(l,r); }
void cd_stream_stop(void)
{
    if (rt.active) spu_sync();
    clock_cancel(rt.event); rt.event=rt.active=0;
    XaDec_Stop();
}
static void stream_event(void *arg)
{
    (void)arg;
    uint8_t sector[CDROM_SECTOR_SIZE1];
    rt.event=0;
    if (!rt.active) return;
    if (rt.lba>=rt.end) { rt.active=0; cd_stream_eof(); XaDec_Stop(); return; }
    const PackEntry *entry=cdrom_find_lba(rt.lba);
    if (entry) {
        cdrom_stream_sector(rt.lba,sector);
        if ((sector[6]&0x24)==0x24 && (drive_mode&0x40)) {
            spu_sync();
            if (!XaDec_Sector(sector+4)) port_trace("xa rejected coding=0x%02x lba=%u",sector[7],rt.lba);
            else if (port_scene_movie_xa) port_scene_movie_xa();
        } else if (!cd_stream_sector(sector)) {
            rt.event=clock_schedule(rt.period,stream_event,NULL); return;
        }
    }
    rt.lba++;
    if (rt.lba==rt.end) { rt.active=0; cd_stream_eof(); XaDec_Stop(); }
    else rt.event=clock_schedule(rt.period,stream_event,NULL);
}
int cd_stream_start(long mode)
{
    PORT_ASSERT(!(mode & 8)); /* SF and CdlSetfilter remain unsupported. */
    cd_stream_stop();
    if (rd.event) {
        clock_cancel(rd.event);
        rd.event=0;
        rd.aborted=1;
    }
    drive_mode=(unsigned char)mode;
    const PackEntry *entry=cdrom_find_lba((uint32_t)pos_to_int(&setloc));
    spu_sync(); /* Drain owed PCM even if the previous stream already reached EOF. */
    XaDec_Reset(); spu_set_cd_input(xa_input,NULL);
    rt.lba=(uint32_t)pos_to_int(&setloc);
    rt.end=entry ? entry->lba+entry->sectors : rt.lba;
    rt.period=(mode&CdlModeSpeed) ? SECTOR_CYCLES_2X : SECTOR_CYCLES_1X;
    rt.active=1; rt.event=clock_schedule(rt.period,stream_event,NULL);
    return 1;
}

/* asm: CdIntToPos -- the inverse; track is left as it is. */
static void int_to_pos(int i, CdlLOC *p)
{
	i += 150;
	p->sector = to_bcd(i % 75);
	p->second = to_bcd(i / 75 % 60);
	p->minute = to_bcd(i / 75 / 60);
}

static uint32_t ps1_address(const void *p)
{
	uintptr_t off = (uintptr_t)p - (uintptr_t)Ps1_Ram;
	return off < PS1_RAM_SIZE ? 0x80000000u | (uint32_t)off : 0; /* 0: not in PS1 RAM */
}

/* A read that starts at the LBA of an overlay file and covers it is the load of that overlay:
 * restore its data (AD-6) once and trace where its bytes landed. */
static void overlay_check(void)
{
	const PackEntry *e = cdrom_find_lba(rd.lba);
	if (e == NULL || e->lba != rd.lba || rd.count < e->sectors || rd.size != CDROM_SECTOR_DATA)
		return;
	const char *name = strrchr(e->path, '\\');
	char file[PACK_PATH_SIZE];
	name = name ? name + 1 : e->path;
	snprintf(file, sizeof(file), "%.*s", (int)strcspn(name, ";"), name);
	int lib = port_overlay_id(file);
	if (lib == 0)
		return;
	port_overlay_loaded(lib);

	uint8_t sha[32];
	char hex[65];
	Sha256 h;
	Sha256_Init(&h);
	Sha256_Update(&h, rd.buf, e->size);
	Sha256_Final(&h, sha);
	for (int i = 0; i < 32; i++)
		snprintf(hex + i * 2, 3, "%02x", sha[i]);
	port_trace("overlay %d %s dest=0x%08x sha256=%s", lib, e->path, ps1_address(rd.buf), hex);
	if (port_overlay_done)
		port_overlay_done();
}

/* One sector reaches memory (asm: cb_read); after the last one, the read callback. Only its
 * file data is written: the console also writes the slack after the end of a file, but the
 * game sizes some buffers to the file (MAPHEAD_SCRIPT_BUFFER: 25000 bytes for a 13-sector
 * read), and past a native global there is another variable, not what followed it in PS1 RAM.
 * [hypothesis: the game never uses those bytes; checked against the reference in story 3.10]
 * On the empty disc nothing is written. */
static void sector_event(void *arg)
{
	static uint8_t sector[CDROM_SECTOR_SIZE1];
	(void)arg;
	rd.event = 0;
	memcpy(rd.buf + rd.done * rd.size, sector, cdrom_read_sector(rd.lba + rd.done, sector, rd.size));
	if (++rd.done < rd.count) {
		rd.event = clock_schedule(rd.sector_cycles, sector_event, NULL);
		return;
	}
	overlay_check();
	if (read_callback)
		read_callback(CdlComplete, read_result);
}

/* asm: CdInit -- CdReset and the default callbacks; the default read callback only delivers a
 * kernel event the game does not open. A read in progress is left to finish [hypothesis: the
 * game never calls CdInit during a read]. */
int CdInit(void)
{
	clock_psyq_entry();
	read_callback = NULL;
	/* asm: CD_initvol (0x800B16B8), called by CdReset(1). Confirmed before
	 * MOV_playMovie in the reference: CD volume 0x3fff and SPUCNT 0xc001. */
	if (!spu_read16(0x1b8) && !spu_read16(0x1ba)) {
		spu_write16(0x180, 0x3fff);
		spu_write16(0x182, 0x3fff);
	}
	spu_write16(0x1b0, 0x3fff);
	spu_write16(0x1b2, 0x3fff);
	spu_write16(0x1aa, 0xc001);
	return 1;
}

static int control(unsigned char com, unsigned char *param)
{
	switch (com) {
	case CdlSetloc:
		if (param)
			memcpy(&setloc, param, 3); /* minute, second, sector */
		return 1;
	case CdlNop:
	    return 1;
    case CdlSetmode:
        PORT_ASSERT(!param || !(*param & 8));
        if (param) drive_mode=*param;
        return 1;
    case CdlPause:
        cd_stream_stop();
        return 1;
	}
	PORT_ASSERT(!"libcd: CD command not modeled");
	return 1;
}

/* asm: CdControl -- 1 when the command is accepted (always: no drive errors). CdControl/B/F
 * and CdSync complete at once, with no command latency [hypothesis: the game does not depend on
 * it; checked against the reference in story 3.10]. */
int CdControl(unsigned char com, unsigned char *param, unsigned char *result)
{
	(void)result;
	clock_psyq_entry();
	return control(com, param);
}

/* asm: CdControlB -- CdControl, then CdSync(0) == CdlComplete. */
int CdControlB(unsigned char com, unsigned char *param, unsigned char *result)
{
	(void)result;
	if (port_scene_movie_timing) port_scene_movie_timing("CdControlB:enter", com);
	clock_psyq_entry();
	int accepted = control(com, param);
	if (port_scene_movie_timing) port_scene_movie_timing("CdControlB:exit", com);
	return accepted;
}

/* asm: CdControlF -- CdControl without waiting for the acknowledge. */
int CdControlF(unsigned char com, unsigned char *param)
{
	clock_psyq_entry();
	return control(com, param);
}

/* asm: CdSync (CD_sync) -- every command completes at once. */
int CdSync(int mode, unsigned char *result)
{
	(void)mode, (void)result;
	clock_psyq_entry();
	return CdlComplete;
}

/* asm: CdRead -> cd_read_retry -- starts reading sectors from the CdlSetloc position into buf
 * and returns 1; a read in progress is abandoned. Sector size from mode & 0x30 (2048 or, with
 * CdlModeSize1, 2340); the first sector arrives after the seek, each one after one sector time. */
int CdRead(int sectors, unsigned long *buf, int mode)
{
	clock_psyq_entry();
    cd_stream_stop();
	uint32_t size = CDROM_SECTOR_DATA;
	switch (mode & (CdlModeSize1 | CdlModeSize0)) {
	case 0:
		break;
	case CdlModeSize1:
		size = CDROM_SECTOR_SIZE1;
		break;
	default:
		PORT_ASSERT(!"libcd: CdlModeSize0 (2328-byte sectors) not modeled");
	}
	clock_cancel(rd.event);
	rd.buf = (uint8_t *)buf;
	rd.lba = (uint32_t)pos_to_int(&setloc);
	rd.count = sectors > 0 ? (uint32_t)sectors : 0;
	rd.done = 0;
	rd.aborted = 0;
	rd.size = size;
	rd.sector_cycles = (mode & CdlModeSpeed) ? SECTOR_CYCLES_2X : SECTOR_CYCLES_1X;
	rd.event = rd.count ? clock_schedule(CDROM_SEEK_CYCLES + rd.sector_cycles, sector_event, NULL) : 0;
	return 1;
}

static int read_finished(void *arg)
{
	(void)arg;
	return rd.aborted || rd.done >= rd.count;
}

/* asm: CdReadSync -- sectors still to read (0 = done); mode 0 waits for the end. result (the
 * last status, from CdReady) is left untouched: the game always passes NULL.
 * An aborted read returns -1, including in blocking mode. */
int CdReadSync(int mode, unsigned char *result)
{
	(void)result;
	clock_psyq_entry();
	if (mode == 0 && !read_finished(NULL)) /* no wait (and no handler check) once it is over */
		clock_wait_until(read_finished, NULL);
	if (rd.aborted) return -1;
	return (int)(rd.count - rd.done);
}

/* asm: CdReadCallback -- called with CdlComplete when the last sector of a read arrives;
 * returns the previous one. */
CdlCB CdReadCallback(CdlCB func)
{
	clock_psyq_entry();
	CdlCB old = read_callback;
	read_callback = func;
	return old;
}

/* asm: CdSearchFile -- NULL unless the name starts with '\'; looks the exact path up in the
 * index and copies its position, ISO size and name (the 0x18 bytes of CdlFILE) to fp. The
 * cached directories and their 64-file limit are not modeled; every search costs a seek and one
 * double-speed sector [hypothesis, story 3.10]. With no Pack (empty disc) nothing is found.
 * Returns fp where the console returns its cache entry: the game only compares it with NULL
 * and -1. */
CdlFILE *CdSearchFile(CdlFILE *fp, char *name)
{
	clock_psyq_entry();
	if (name[0] != '\\')
		return NULL;
	/* CdSearchFile (800B2440..24C4) resolves each leading empty component through
	 * the ISO root path-table entry (parent 1, empty name). readFileSection uses two. */
	while (name[1] == '\\') name++;
	clock_advance(CDROM_SEEK_CYCLES + SECTOR_CYCLES_2X);
	const PackEntry *e = cdrom_find_path(name);
	if (e == NULL)
		return NULL;
	const char *base = strrchr(e->path, '\\') + 1;
	int_to_pos((int)e->lba, &fp->pos);
	/* F2: the Manifest keeps the stored size; the ISO size is sectors * 2048 [hypothesis: true of
	 * every F2 file, checked only when the Pack is built (story 2.7); checked again in 3.10]. */
	fp->size = e->mode == PACK_F2 ? e->sectors * CDROM_SECTOR_DATA : e->size;
	memset(fp->name, 0, sizeof(fp->name));
	strncpy(fp->name, base, sizeof(fp->name) - 1);
	return fp;
}

CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
	clock_psyq_entry();
	int_to_pos(i, p);
	return p;
}

int CdPosToInt(CdlLOC *p)
{
	clock_psyq_entry();
	return pos_to_int(p);
}

/* Back to the state before boot (tests). */
void psyq_libcd_reset(void)
{
    clock_cancel(rt.event); memset(&rt,0,sizeof(rt));
    cd_stream_reset(); XaDec_Reset(); drive_mode=0;
	clock_cancel(rd.event);
	memset(&rd, 0, sizeof(rd));
	memset(&setloc, 0, sizeof(setloc));
	read_callback = NULL;
}
