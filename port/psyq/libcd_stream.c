/* STR ring adapted from Wyrelade/DW2-PC psyq/libcd.c (CC0 1.0),
 * checked against asm/us/main/psyq/libcd.s. The console reserves 32 bytes of
 * header and 2016 bytes of payload for each of the size ring sectors. */
#include "psyq.h"
#include "clock.h"
#include "port.h"
#include "libcd_stream.h"
#include <string.h>

static uint8_t *ring;
static uint32_t size, producer, consumer, building, chunks, next_chunk;
static uint32_t start_frame, end_frame;
static int ended;
static uint32_t last_frame;
static CdlLOC last_loc;
static StHEADER terminal = {.frameCount = 0x7fffffff, .width = 320, .height = 240};
/* Empty v2 frame: copied header, DC terminator, and refill padding. */
static unsigned long terminal_data[4] = {0x38000000, 0x00020001, 0x00007fc0, 0};
static void (*first_cb)(void), (*end_cb)(void);
static StHEADER *header(uint32_t slot) { return (StHEADER *)(ring + slot * 32); }
static uint8_t *data(uint32_t slot) { return ring + size * 32 + slot * 2016; }
static uint16_t le16(const uint8_t *p) { return p[0] | p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p+2) << 16; }
void cd_stream_reset(void)
{
    ring = NULL; size = producer = consumer = chunks = next_chunk = 0;
    start_frame = end_frame = last_frame = 0; ended = 0;
    memset(&last_loc,0,sizeof(last_loc));
    terminal=(StHEADER){.frameCount=0x7fffffff,.width=320,.height=240}; first_cb = end_cb = NULL;
}
/* asm: StCdInterrupt (800B3468): classify header, reserve contiguous chunks,
 * publish only complete frames. Return 0 applies backpressure without consuming LBA. */
int cd_stream_sector(const uint8_t *s)
{
    const uint8_t *d = s + 12;
    if (!ring || !size || le32(d) != 0x80010160) return 1;
    unsigned cn = le16(d+4), cc = le16(d+6);
    uint32_t frame = le32(d+8);
    if (!cc || cc > size || cn >= cc || frame < start_frame) return 1;
    if (end_frame && frame >= end_frame) { cd_stream_eof(); return 1; }
    if (!cn) {
        if (chunks) { /* truncated previous frame is never published */
            for (unsigned i=0;i<chunks;i++) header(building+i)->id=0;
            chunks=0;
        }
        uint32_t slot = producer;
        if (slot + cc > size) slot = 0;
        for (unsigned i=0;i<cc;i++) if (header(slot+i)->id) return 0;
        if (slot != producer) {
            for (unsigned i=producer;i<size;i++) {
                if (header(i)->id) return 0;
            }
            header(producer)->id = 1; /* wrap marker, asm StGetNext */
        }
        building=slot; chunks=cc; next_chunk=0;
        for (unsigned i=0;i<cc;i++) header(slot+i)->id=3;
        memcpy(header(slot), d, 32);
        header(slot)->id=3;
    }
    if (!chunks || cn != next_chunk || cc != chunks || frame != header(building)->frameCount) return 1;
    /* asm: StCdInterrupt 800B371C updates the location for each delivered chunk. */
    memcpy(&header(building)->loc, s, 3);
    memcpy(data(building) + cn * 2016, d+32, 2016);
    if (++next_chunk == chunks) {
        header(building)->id=2;
        last_frame=header(building)->frameCount; last_loc=header(building)->loc;
        terminal=*header(building); terminal.frameCount=0x7fffffff;
        producer=(building+chunks)%size; chunks=0;
        if (first_cb) first_cb();
    }
    return 1;
}
void cd_stream_eof(void)
{
    if (ended) return;
    if (chunks) {
        for (unsigned i=0;i<chunks;i++) header(building+i)->id=0;
        chunks=0;
    }
    ended=1;
    if (end_cb) end_cb();
}
/* asm: CdRead2, 800B2DE0: Setmode then ReadS; mode bit 8 installs STR callbacks. */
int CdRead2(long mode) { clock_psyq_entry(); ended=0; return cd_stream_start(mode); }
/* asm: StSetRing, 800AFD78 / StClearRing. Size is in 2048-byte sectors. */
void StSetRing(unsigned long *addr, unsigned long count)
{
    clock_psyq_entry(); ring=(uint8_t *)addr; size=addr ? count : 0;
    producer=consumer=chunks=next_chunk=0; ended=0;
    last_frame=0; memset(&last_loc,0,sizeof(last_loc));
    terminal=(StHEADER){.frameCount=0x7fffffff,.width=320,.height=240};
    if (ring) memset(ring,0,size*32);
}
/* asm: StUnSetRing, 800B2EC4: remove callbacks and clear ring state. */
void StUnSetRing(void) { clock_psyq_entry(); cd_stream_stop(); cd_stream_reset(); }
/* asm: StSetStream, 800B3034 / StSetMask, 800B3274. */
void StSetStream(unsigned long mode, unsigned long start, unsigned long end, void (*first)(), void (*second)())
{
    clock_psyq_entry(); (void)mode;
    start_frame=start; end_frame=end; first_cb=first; end_cb=second;
}
/* asm: StCdInterrupt, 800B3468. Clock events deliver sectors directly; no IRQ polling. */
void StCdInterrupt(void) { clock_psyq_entry(); }
/* asm: StFreeRing, 800B30C4. Only a borrowed frame (status 4) can be released. */
unsigned long StFreeRing(unsigned long *base)
{
    clock_psyq_entry();
    if (!ring || (uintptr_t)base < (uintptr_t)data(0) || (uintptr_t)base >= (uintptr_t)data(size)) return 1;
    uintptr_t off=(uintptr_t)base-(uintptr_t)data(0);
    if (off%2016) return 1;
    unsigned slot=off/2016, count=header(slot)->nSectors;
    if (header(slot)->id!=4 || !count || count>size-slot) return 1;
    for (unsigned i=0;i<count;i++) header(slot+i)->id=0;
    consumer=(slot+count)%size;
    return 0;
}
/* asm: StGetBackloc, 800B2FD0. Recover at the next sector after the last frame. */
int StGetBackloc(CdlLOC *loc)
{
    clock_psyq_entry();
    if (loc) {
        *loc=last_loc;
        unsigned n=((loc->minute/16*10+loc->minute%16)*60+
                    loc->second/16*10+loc->second%16)*75+loc->sector/16*10+loc->sector%16+1;
        unsigned m=n/4500, s=n/75%60, f=n%75;
        loc->minute=m/10*16+m%10; loc->second=s/10*16+s%10; loc->sector=f/10*16+f%10;
    }
    return (int)last_frame; /* asm: next_frameid is the published frame ID, without +1. */
}
/* asm: StGetNext, 800B31B4: status 2 -> 4; consumer advances only on free.
 * The empty-ring path is CPU polling, not a sector-sized device wait. */
unsigned long StGetNext(unsigned long **addr, unsigned long **head)
{
    clock_advance_cost(CLOCK_CD_STREAM_POLL_CYCLES, CLOCK_COST_CPU);
    if (!ring || !size) return 1;
    if (header(consumer)->id==1) { header(consumer)->id=0; consumer=0; }
    if (header(consumer)->id==2) {
        clock_advance_cost(CLOCK_CD_STREAM_PUBLISH_CYCLES, CLOCK_COST_CPU);
        header(consumer)->id=4;
        *addr=(unsigned long *)data(consumer); *head=(unsigned long *)header(consumer);
        return 0;
    }
    if (ended && header(consumer)->id!=4) {
        *addr=terminal_data; *head=(unsigned long *)&terminal; return 0;
    }
    return 1;
}
