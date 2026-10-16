#include "psyq.h"
#include "clock.h"

/* stub-m2: silent shutdown; no sequencer state. */
void SsEnd(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent initialization; no sequencer state. */
void SsInit(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent shutdown; no resources. */
void SsQuit(void)
{
    clock_psyq_entry();
}

/* stub-m2: silent start; no sequencer. */
void SsStart(void)
{
    clock_psyq_entry();
}

/* stub-m2: close silently; no resources. */
void SsSeqClose(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: stop silently. */
void SsSeqStop(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: close silently; no SPU allocation. */
void SsVabClose(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: silence all voices; no state. */
void SsUtAllKeyOff(short id)
{
    clock_psyq_entry();
    (void)id;
}

/* stub-m2: accept playback without sound. */
void SsSeqPlay(short id, char mode, short repeat)
{
    clock_psyq_entry();
    (void)id;
    (void)mode;
    (void)repeat;
}

/* stub-m2: ignore sequence volume. */
void SsSeqSetVol(short id, short left, short right)
{
    clock_psyq_entry();
    (void)id;
    (void)left;
    (void)right;
}

/* stub-m2: ignore master volume. */
void SsSetMVol(short left, short right)
{
    clock_psyq_entry();
    (void)left;
    (void)right;
}

/* stub-m2: ignore sequencer table. */
void SsSetTableSize(char *table, short seqs, short seps)
{
    clock_psyq_entry();
    (void)table;
    (void)seqs;
    (void)seps;
}

/* stub-m2: ignore sequencer tick mode. */
void SsSetTickMode(long mode)
{
    clock_psyq_entry();
    (void)mode;
}

/* stub-m2: return silent sequence id 0. */
short SsSeqOpen(unsigned long *data, short vabid)
{
    clock_psyq_entry();
    (void)data;
    (void)vabid;
    return 0;
}

/* stub-m2: return the requested vabid without allocating SPU memory. */
short SsVabOpenHeadSticky(unsigned char *data, short vabid, unsigned long addr)
{
    clock_psyq_entry();
    (void)data;
    (void)addr;
    return vabid;
}

/* stub-m2: return the requested vabid without transferring bytes. */
short SsVabTransBody(unsigned char *data, short vabid)
{
    clock_psyq_entry();
    (void)data;
    return vabid;
}

/* stub-m2: return 1 immediately for either wait mode. */
short SsVabTransCompleted(short mode)
{
    clock_psyq_entry();
    (void)mode;
    return 1;
}

/* stub-m2: return 0; no SPU memory. */
unsigned long SsUtGetVBaddrInSB(short vabid)
{
    clock_psyq_entry();
    (void)vabid;
    return 0;
}

/* stub-m2: return 0 without sound. */
short SsUtKeyOffV(short voice)
{
    clock_psyq_entry();
    (void)voice;
    return 0;
}

/* stub-m2: return 0 without changing volume. */
short SsUtAutoVol(short voice, short start, short end, short time)
{
    clock_psyq_entry();
    (void)voice;
    (void)start;
    (void)end;
    (void)time;
    return 0;
}

/* stub-m2: return 0 without sound. */
short SsUtKeyOnV(short voice, short vabid, short prog, short tone, short note, short fine, short left, short right)
{
    clock_psyq_entry();
    (void)voice;
    (void)vabid;
    (void)prog;
    (void)tone;
    (void)note;
    (void)fine;
    (void)left;
    (void)right;
    return 0;
}

