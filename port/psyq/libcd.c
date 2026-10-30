__attribute__((weak)) void port_scene_movie_xa(void);
__attribute__((weak)) void port_scene_movie_timing(const char *event, unsigned value);
/* libcd: commands, data reads and file search over the CD model (port/backend/cdrom.h) and the
 * virtual clock (AD-8). asm:asm/us/main/psyq/libcd.s (mw_lib41). Not modeled: CD-DA, media read errors, the shell-open status. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../backend/cdrom.h"
#include "../host/pack/sha256.h"
#include "clock.h"
#include "clock_costs.h"
#include "overlay.h"
#include "libcd_stream.h"
#include "../backend/xa.h"
#include "../backend/spu.h"
#include "port.h"
#include "ps1mem.h"
#include "psyq.h"

_Static_assert(sizeof(CdlFILE) == 0x18 && offsetof(CdlFILE, size) == 4 && offsetof(CdlFILE, name) == 8,
               "CdlFILE as in libcd.h (CdSearchFile copies 0x18 bytes)");

/* Runtime hook after an overlay load (crt.c, --until-overlay); absent in unit tests. */
__attribute__((weak)) void port_overlay_done(void);

static CdlLOC setloc;
static unsigned char drive_mode;
static uint32_t head_lba, cached_directory;
static int speed_changed, path_table_loaded;
static uint64_t command_busy_until;
static int command_waiting;
/* Detaching STR callbacks does not issue a drive Pause command. Keep the
 * transport state even after the host stops delivering sectors to the ring. */
static int stream_reading;
static struct { uint32_t lba, end; uint64_t period, next_sector; int event, active; } rt;
static CdlCB read_callback;
static unsigned char read_result[8]; /* status bytes passed to the read callback */
static int pause_ack_event, pause_complete_event;
extern int psyq_spu_dma_irq_active(void);
static unsigned cd_irq_route_discount(void)
{
    return psyq_spu_dma_irq_active() ? 0 : CLOCK_CD_NO_DMA_ROUTE_CPU_CYCLES;
}
static void pause_ack_irq(void *arg)
{
    (void)arg; pause_ack_event=0;
    clock_advance_cost(CLOCK_CD_ACK_IRQ_CPU_CYCLES - cd_irq_route_discount(), CLOCK_COST_CPU);
}
static void pause_complete_irq(void *arg)
{
    (void)arg; pause_complete_event=0;
    clock_advance_cost(CLOCK_CD_COMPLETE_IRQ_CPU_CYCLES - cd_irq_route_discount() +
        (command_waiting ? CLOCK_CD_COMPLETE_WAIT_CPU_CYCLES : 0), CLOCK_COST_CPU);
}

static void command_ready(void)
{
    if (command_busy_until > clock_now()) {
        command_waiting = 1;
        clock_advance_cost(command_busy_until-clock_now(), CLOCK_COST_CD);
        command_waiting = 0;
    }
}
static void command_ack(void)
{
    command_ready();
    clock_advance_cost(CLOCK_CD_COMMAND_CPU_CYCLES, CLOCK_COST_CPU);
    clock_advance_cost(CLOCK_CD_COMMAND_CYCLES, CLOCK_COST_CD);
}
static void set_mode(unsigned char mode)
{
    if ((mode ^ drive_mode) & CdlModeSpeed) speed_changed=1;
    drive_mode=mode;
}
static uint64_t read_delay(uint32_t lba, uint64_t sector_cycles)
{
    uint32_t distance = head_lba>lba ? head_lba-lba : lba-head_lba;
    uint64_t delay = CLOCK_CD_COMMAND_CYCLES + CLOCK_CD_READ_START_CYCLES +
        CLOCK_CD_READ_SEEK_CYCLES + CLOCK_CD_US((uint64_t)distance*CLOCK_CD_SEEK_LBA_US) + sector_cycles;
    if (speed_changed) delay += CLOCK_CD_SPEED_CHANGE_CYCLES;
    if (getenv("DW1_CD_TIMING"))
        fprintf(stderr,"cd-read cycle=%llu head=%u target=%u period=%llu speed_change=%d delay=%llu\n",
            (unsigned long long)clock_now(),head_lba,lba,(unsigned long long)sector_cycles,
            speed_changed,(unsigned long long)delay);
    speed_changed=0;
    return delay;
}
static void pause_transport(uint64_t period)
{
    clock_cancel(pause_ack_event); clock_cancel(pause_complete_event);
    pause_ack_event=clock_schedule(CLOCK_CD_COMMAND_CYCLES,pause_ack_irq,NULL);
    pause_complete_event=clock_schedule(CLOCK_CD_COMMAND_CYCLES+
        (period==CLOCK_CD_SECTOR_2X_CYCLES ? CLOCK_CD_PAUSE_2X_CYCLES : CLOCK_CD_PAUSE_1X_CYCLES),
        pause_complete_irq,NULL);
    command_busy_until=clock_now()+CLOCK_CD_COMMAND_CYCLES+
        (period==CLOCK_CD_SECTOR_2X_CYCLES ? CLOCK_CD_PAUSE_2X_CYCLES : CLOCK_CD_PAUSE_1X_CYCLES);
}

/* The read in progress (asm: ReadAttr). */
static struct {
	uint8_t *buf;
	uint32_t lba;   /* first sector */
	uint32_t count; /* sectors asked */
	uint32_t done;  /* sectors delivered */
	uint32_t size;  /* bytes per sector */
	uint64_t sector_cycles, next_sector, seek_cycles;
	unsigned start_phase;
	int event;      /* pending sector event, 0 if none */
	int aborted;
	uint64_t retry_deadline;
    int polling;
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
            rt.next_sector += rt.period;
            rt.event=clock_schedule(rt.next_sector > clock_now() ? rt.next_sector-clock_now() : 0,stream_event,NULL); return;
        }
    }
    head_lba=++rt.lba;
    if (rt.lba==rt.end) { rt.active=0; cd_stream_eof(); XaDec_Stop(); }
    else {
        rt.next_sector += rt.period;
        rt.event=clock_schedule(rt.next_sector > clock_now() ? rt.next_sector-clock_now() : 0,stream_event,NULL);
    }
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
    command_ready();
    if (drive_mode!=(unsigned char)mode) { command_ack(); set_mode((unsigned char)mode); }
    stream_reading=1;
    const PackEntry *entry=cdrom_find_lba((uint32_t)pos_to_int(&setloc));
    spu_sync(); /* Drain owed PCM even if the previous stream already reached EOF. */
    XaDec_Reset(); spu_set_cd_input(xa_input,NULL);
    rt.lba=(uint32_t)pos_to_int(&setloc);
    rt.end=entry ? entry->lba+entry->sectors : rt.lba;
    rt.period=((mode&CdlModeSpeed) ? CLOCK_CD_US(6666u) : CLOCK_CD_US(13333u)) +
        CLOCK_CD_STREAM_SECTOR_DISPATCH_CYCLES;
    rt.active=1; rt.next_sector=clock_now()+read_delay(rt.lba,rt.period);
    rt.event=clock_schedule(rt.next_sector-clock_now(),stream_event,NULL);
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

static void sector_event(void *arg);
static uint64_t read_physical_dispatch(void)
{
    uint64_t physical = rd.next_sector;
    if (rd.polling && physical <= clock_poll_boundary()) return clock_poll_boundary();
    return physical + (clock_now() > physical ? CLOCK_CD_MASKED_SECTOR_DISPATCH_CYCLES : CLOCK_CD_SECTOR_DISPATCH_CYCLES);
}
static void read_start_event(void *arg)
{
    (void)arg;
    uint64_t physical = read_physical_dispatch();
    if (physical > clock_now()) clock_advance_cost(physical-clock_now(), CLOCK_COST_CPU);
    if (rd.start_phase++ == 0) {
        rd.next_sector = physical + CLOCK_CD_READ_START_CYCLES;
        clock_advance_cost(CLOCK_CD_READ_ACK_IRQ_CPU_CYCLES-cd_irq_route_discount(), CLOCK_COST_CPU);
        rd.event = clock_schedule(rd.next_sector > clock_now() ? rd.next_sector-clock_now() : 0, read_start_event, NULL);
    } else {
        rd.next_sector = physical + rd.seek_cycles + rd.sector_cycles - CLOCK_CD_SECTOR_DISPATCH_CYCLES;
        rd.event = clock_schedule(rd.next_sector > clock_now() ? rd.next_sector-clock_now() : 0, sector_event, NULL);
    }
}
static void schedule_read_start(void)
{
    uint64_t total = read_delay(rd.lba+rd.done,rd.sector_cycles);
    rd.seek_cycles = total-CLOCK_CD_COMMAND_CYCLES-CLOCK_CD_READ_START_CYCLES-rd.sector_cycles;
    rd.start_phase = 0;
    rd.next_sector = clock_now()+CLOCK_CD_COMMAND_CYCLES;
    rd.event = rd.count ? clock_schedule(CLOCK_CD_COMMAND_CYCLES,read_start_event,NULL) : 0;
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
	/* ReadN's physical callback runs at the foreground branch test. BIOS
	 * delivery can be masked by an older IRQ without moving that deadline. */
    uint64_t physical = read_physical_dispatch();
    if (getenv("DW1_CD_TIMING")) fprintf(stderr,"cd-sector physical=%llu delay=%llu masked=%d route=%u\n",
        (unsigned long long)physical,(unsigned long long)(physical-rd.next_sector),clock_now()>rd.next_sector,cd_irq_route_discount());
    if (physical>clock_now()) clock_advance_cost(physical-clock_now(),CLOCK_COST_CPU);
    uint64_t next_sector = physical + rd.sector_cycles - CLOCK_CD_SECTOR_DISPATCH_CYCLES;
	rd.next_sector = next_sector;
	clock_advance_cost(rd.done + 1 == rd.count ?
                      CLOCK_CD_LAST_READY_IRQ_CPU_CYCLES - CLOCK_CD_LAST_READY_RETURN_CPU_CYCLES - 2 * cd_irq_route_discount() :
	                  CLOCK_CD_READY_IRQ_CPU_CYCLES - cd_irq_route_discount(), CLOCK_COST_CPU);
	memcpy(rd.buf + rd.done * rd.size, sector, cdrom_read_sector(rd.lba + rd.done, sector, rd.size));
	head_lba=rd.lba+rd.done+1;
	rd.retry_deadline = clock_vblank_cycle(clock_vblanks() + CLOCK_CD_READ_RETRY_VBLANKS);
	if (++rd.done < rd.count) {
		rd.event = clock_schedule(next_sector > clock_now() ? next_sector-clock_now() : 0, sector_event, NULL);
		return;
	}
	pause_transport(rd.sector_cycles); /* cb_read issues CdControlF(Pause). */
	clock_advance_cost(CLOCK_CD_LAST_READY_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
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
	/* Init replaces the pending Pause completion. map_load Redux issues
	 * Nop at 642778514 after Pause at 642645604, well before its 35 ms
	 * completion. Waiting for that old completion serializes unrelated work. */
	command_busy_until = clock_now();
	clock_cancel(pause_ack_event); clock_cancel(pause_complete_event);
	pause_ack_event=pause_complete_event=0;
	clock_advance_cost(CLOCK_CD_INIT_CPU_CYCLES, CLOCK_COST_CPU);
	command_ack(); /* Nop */
	command_ack(); /* Init */
	clock_advance_cost(CLOCK_CD_INIT_CYCLES, CLOCK_COST_CD);
	command_ack(); /* Demute */
	head_lba=0;
	drive_mode=0;
	speed_changed=0;
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
        if (param) set_mode(*param);
        return 1;
    case CdlPause:
        cd_stream_stop();
        stream_reading=0;
        if (rd.event) {
            clock_cancel(rd.event);
            rd.event = 0;
            rd.aborted = 1;
        }
        return 1;
	}
	PORT_ASSERT(!"libcd: CD command not modeled");
	return 1;
}

/* asm: CdControl -- 1 when the command is accepted (always: no drive errors). CdControl/B/F
 * and CdSync complete at once except blocking Pause below [hypothesis: other
 * command latencies remain to be checked in story 3.10]. */
int CdControl(unsigned char com, unsigned char *param, unsigned char *result)
{
    if (getenv("DW1_DEVICE_PROBE")) fprintf(stderr,"cd-control %llu %u\n",(unsigned long long)clock_now(),com);
	(void)result;
	clock_psyq_entry();
    if (com == CdlSetloc) {
        command_ready();
        clock_advance_cost(CLOCK_CD_SETLOC_SETUP_CPU_CYCLES, CLOCK_COST_CPU);
        clock_advance_cost(CLOCK_CD_COMMAND_CYCLES, CLOCK_COST_CD);
        clock_advance_cost(CLOCK_CD_SETLOC_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
    } else {
        command_ack();
    }
	uint64_t period=(drive_mode&CdlModeSpeed) ? CLOCK_CD_SECTOR_2X_CYCLES : CLOCK_CD_SECTOR_1X_CYCLES;
	int reading=stream_reading || rd.event;
	if (com==CdlPause) command_busy_until=clock_now()+(reading ?
        (period==CLOCK_CD_SECTOR_2X_CYCLES ? CLOCK_CD_PAUSE_2X_CYCLES : CLOCK_CD_PAUSE_1X_CYCLES) : CLOCK_CD_PAUSE_IDLE_CYCLES);
	return control(com, param);
}

/* asm: CdControlB -- CdControl, then CdSync(0) == CdlComplete. */
int CdControlB(unsigned char com, unsigned char *param, unsigned char *result)
{
	(void)result;
	if (port_scene_movie_timing) port_scene_movie_timing("CdControlB:enter", com);
	int accepted = CdControl(com, param, result);
    command_ready();
	if (port_scene_movie_timing) port_scene_movie_timing("CdControlB:exit", com);
	return accepted;
}

/* asm: CdControlF -- CdControl without waiting for the acknowledge. */
int CdControlF(unsigned char com, unsigned char *param)
{
	clock_psyq_entry();
	command_ready();
	clock_advance_cost(CLOCK_CD_COMMAND_CPU_CYCLES, CLOCK_COST_CPU);
	if (com==CdlPause) {
        if (stream_reading || rd.event) pause_transport((drive_mode&CdlModeSpeed) ? CLOCK_CD_SECTOR_2X_CYCLES : CLOCK_CD_SECTOR_1X_CYCLES);
        else command_busy_until=clock_now()+CLOCK_CD_COMMAND_CYCLES+CLOCK_CD_PAUSE_IDLE_CYCLES;
    }
	return control(com, param);
}

/* asm: CdSync (CD_sync) -- every command completes at once. */
int CdSync(int mode, unsigned char *result)
{
	(void)result;
	clock_psyq_entry();
	if (!mode) command_ready();
	else if (command_busy_until>clock_now()) return 0; /* CdlNoIntr */
	return CdlComplete;
}

/* asm: CdRead -> cd_read_retry -- starts reading sectors from the CdlSetloc position into buf
 * and returns 1; a read in progress is abandoned. Sector size from mode & 0x30 (2048 or, with
 * CdlModeSize1, 2340); the first sector arrives after the seek, each one after one sector time. */
int CdRead(int sectors, unsigned long *buf, int mode)
{
    if (getenv("DW1_DEVICE_PROBE")) fprintf(stderr,"cd-read-api %llu %d\n",(unsigned long long)clock_now(),sectors);
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
	command_ready();
    clock_advance_cost(CLOCK_CD_READ_SETUP_CPU_CYCLES, CLOCK_COST_CPU);
	/* CdRead forces raw-sector delivery in the controller (800B2C28),
	 * independently of the requested size copied to the caller. */
	unsigned char controller_mode = (unsigned char)mode | CdlModeSize1;
	if (controller_mode != drive_mode) { command_ack(); set_mode(controller_mode); }
	rd.buf = (uint8_t *)buf;
	rd.lba = (uint32_t)pos_to_int(&setloc);
	rd.count = sectors > 0 ? (uint32_t)sectors : 0;
	rd.done = 0;
	rd.aborted = 0;
	rd.size = size;
	rd.sector_cycles = (mode & CdlModeSpeed) ? CLOCK_CD_SECTOR_2X_CYCLES : CLOCK_CD_SECTOR_1X_CYCLES;
	rd.retry_deadline = clock_vblank_cycle(clock_vblanks() + CLOCK_CD_READ_RETRY_VBLANKS);
	schedule_read_start();
    clock_advance_cost(CLOCK_CD_READ_RETURN_CPU_CYCLES, CLOCK_COST_CPU);
	return 1;
}

static int read_finished(void *arg)
{
	(void)arg;
	if (!rd.aborted && rd.done < rd.count && clock_now() >= rd.retry_deadline) {
		/* Redux moves the physical head at the start of the seek, before its
		 * delay expires. PsyQ's Pause/Setloc/Setmode/ReadN retry therefore seeks
		 * from that destination, with the speed transition already consumed.
		 * This runs in CdReadSync's polling loop, never in a device IRQ. */
		PORT_ASSERT(!clock_in_handler());
		clock_cancel(rd.event);
		head_lba = rd.lba + rd.done;
		command_ack(); /* Pause ACK */
		pause_transport(rd.sector_cycles);
		command_ack(); /* Setloc, waiting for Pause completion */
		command_ack(); /* Setmode */
		schedule_read_start();
		rd.retry_deadline = clock_vblank_cycle(clock_vblanks() + CLOCK_CD_READ_RETRY_VBLANKS);
	}
	return rd.aborted || rd.done >= rd.count;
}

extern void psyq_vsync_query_cost(void);

/* asm: CdReadSync -- sectors still to read (0 = done); mode 0 waits for the end. result (the
 * last status, from CdReady) is left untouched: the game always passes NULL.
 * An aborted read returns -1, including in blocking mode. */

int CdReadSync(int mode, unsigned char *result)
{
    (void)result;
    if (getenv("DW1_DEVICE_PROBE")) fprintf(stderr,"read-sync enter %llu\n",(unsigned long long)clock_now());
    if (mode || clock_in_handler()) {
        clock_psyq_entry();
        read_finished(NULL);
        return rd.aborted ? -1 : (int)(rd.count-rd.done);
    }
    rd.polling = 1;
    clock_poll_branch(CLOCK_CD_READ_POLL_SETUP_CPU_CYCLES);
    int remaining;
    do {
        clock_poll_branch(CLOCK_CD_POLL_CALL_CPU_CYCLES);
        psyq_vsync_query_cost();
        read_finished(NULL);
        clock_poll_branch(CLOCK_CD_POLL_SECOND_CALL_CPU_CYCLES);
        psyq_vsync_query_cost();
        clock_poll_branch(CLOCK_CD_POLL_TIMEOUT_CPU_CYCLES);
        remaining = rd.aborted ? -1 : (int)(rd.count-rd.done);
        clock_poll_branch(CLOCK_CD_POLL_CONTINUE_CPU_CYCLES);
    } while (remaining > 0);
    rd.polling = 0;
    /* CdReady(1, result), followed by the five-register epilogue. */
    clock_poll_branch(CLOCK_CD_POLL_CALL_CPU_CYCLES);
    clock_advance_cost(CLOCK_CD_READY_QUERY_CPU_CYCLES, CLOCK_COST_CPU);
    clock_poll_branch(CLOCK_CD_READ_SYNC_EPILOGUE_CPU_CYCLES);
    if (getenv("DW1_DEVICE_PROBE")) fprintf(stderr,"read-sync exit %llu\n",(unsigned long long)clock_now());
    return remaining;
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
 * path-table cache and the last directory are charged as physical reads using
 * verified ISO metadata. Unknown manifests use the provisional seek model.
 * With no Pack (empty disc) nothing is found.
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
	uint32_t directory=cdrom_directory_lba(name);
	if (directory) {
        /* CdSearchFile caches the path table once, and one directory at a time.
         * These reads move the same physical head as file/STR transfers. */
        uint32_t reads[3]={16,cdrom_path_table_lba(),directory};
        unsigned first=path_table_loaded ? 2 : 0;
        if (!path_table_loaded) {
            /* map_load first-medium trace: Pause, Setloc, Setmode(0),
             * ReadN precede the descriptor read. The stale read is replaced
             * before it transfers a sector. Subsequent cached searches do
             * not repeat this controller initialization. */
            command_ack(); /* idle Pause */
            clock_advance_cost(CLOCK_CD_PAUSE_IDLE_CYCLES, CLOCK_COST_CD);
            command_ack(); /* initial Setloc */
            command_ack(); /* Setmode(0) */
            set_mode(0);
            command_ack(); /* replaced ReadN */
        }
        if (!path_table_loaded || directory!=cached_directory) {
            for (unsigned i=first; i<3; i++) {
                command_ack(); /* Setloc */
                if (drive_mode != (CdlModeSpeed | CdlModeSize1)) {
                    command_ack(); set_mode(CdlModeSpeed | CdlModeSize1);
                }
                clock_advance_cost(read_delay(reads[i],CLOCK_CD_SECTOR_2X_CYCLES),CLOCK_COST_CD);
                head_lba=reads[i]+1;
                pause_transport(CLOCK_CD_SECTOR_2X_CYCLES);
                unsigned copies, bytes;
                cdrom_iso_copy_counts(reads[i], &copies, &bytes);
                uint64_t parse = 0;
                if (reads[i] == cdrom_path_table_lba())
                    parse = CLOCK_CD_PATH_PARSE_FIXED_CYCLES +
                        (uint64_t)copies * CLOCK_CD_PATH_PARSE_RECORD_CYCLES;
                else if (copies)
                    parse = CLOCK_CD_DIRECTORY_PARSE_FIXED_CYCLES +
                        (uint64_t)(copies + 2) * CLOCK_CD_DIRECTORY_PARSE_RECORD_CYCLES;
                clock_advance_cost(parse + (uint64_t)copies * CLOCK_LIBC_MEMCPY_FIXED_CYCLES +
                                   (uint64_t)bytes * CLOCK_LIBC_MEMCPY_BYTE_CYCLES,
                                   CLOCK_COST_CPU);
            }
            path_table_loaded=1; cached_directory=directory;
        }
    } else {
        /* Unknown/synthetic manifests have no verified directory metadata. */
        clock_advance_cost(CLOCK_CD_SEEK_CYCLES+CLOCK_CD_SECTOR_2X_CYCLES,CLOCK_COST_CD);
    }
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

/* asm: CdIntToPos -- sector number to BCD minute/second/sector. */
CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
	clock_advance_cost(CLOCK_CD_INT_TO_POS_CPU_CYCLES, CLOCK_COST_CPU);
	int_to_pos(i, p);
	return p;
}

/* asm: CdPosToInt -- BCD minute/second/sector to sector number. */
int CdPosToInt(CdlLOC *p)
{
	clock_psyq_entry();
	return pos_to_int(p);
}

/* Back to the state before boot (tests). */
void psyq_libcd_reset(void)
{
    clock_cancel(pause_ack_event); clock_cancel(pause_complete_event);
    pause_ack_event=pause_complete_event=0;
    clock_cancel(rt.event); memset(&rt,0,sizeof(rt));
    cd_stream_reset(); XaDec_Reset(); drive_mode=0; stream_reading=0;
    head_lba=cached_directory=0; speed_changed=path_table_loaded=0; command_busy_until=0;
	clock_cancel(rd.event);
	memset(&rd, 0, sizeof(rd));
	memset(&setloc, 0, sizeof(setloc));
	read_callback = NULL;
}
