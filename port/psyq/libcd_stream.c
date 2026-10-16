#include "psyq.h"
#include "clock.h"

static StHEADER header = { .frameCount = 0xFFFFFFFFUL, .width = 320, .height = 240 };
static unsigned long bitstream[1];

/* stub-m2: return 1; synthetic stream starts immediately. */
int CdRead2(long mode)
{
    clock_psyq_entry();
    (void)mode;
    return 1;
}

/* stub-m2: ignore the ring; synthetic storage is static. */
void StSetRing(unsigned long *ring, unsigned long size)
{
    clock_psyq_entry();
    (void)ring;
    (void)size;
}

/* stub-m2: no ring storage to release. */
void StUnSetRing(void)
{
    clock_psyq_entry();
}

/* stub-m2: ignore stream selection; always use the terminal black frame. */
void StSetStream(unsigned long mode, unsigned long start, unsigned long end, void (*first)(), void (*second)())
{
    clock_psyq_entry();
    (void)mode;
    (void)start;
    (void)end;
    (void)first;
    (void)second;
}

/* stub-m2: no CD interrupt work for the synthetic stream. */
void StCdInterrupt(void)
{
    clock_psyq_entry();
}

/* stub-m2: return 0; static bitstream is never freed. */
unsigned long StFreeRing(unsigned long *base)
{
    clock_psyq_entry();
    (void)base;
    return 0;
}

/* stub-m2: return 0 and a zero synthetic location. */
int StGetBackloc(CdlLOC *loc)
{
    clock_psyq_entry();
    if (loc) *loc = header.loc;
    return 0;
}

/* stub-m2: return 0 with the same static 320x240 frame, frameCount 0xFFFFFFFF. */
unsigned long StGetNext(unsigned long **addr, unsigned long **head)
{
    clock_psyq_entry();
    *addr = bitstream;
    *head = (unsigned long *)&header;
    return 0;
}

