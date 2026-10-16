#include "psyq.h"
#include "clock.h"

/* stub-m2: return 0 without touching SPU memory. */
long SpuClearReverbWorkArea(long mode)
{
    clock_psyq_entry();
    (void)mode;
    return 0;
}

/* stub-m2: always return SPU_OFF. */
long SpuGetKeyStatus(unsigned long voice)
{
    clock_psyq_entry();
    (void)voice;
    return SPU_OFF;
}

/* stub-m2: return 0 without reverb. */
long SpuSetReverb(long on)
{
    clock_psyq_entry();
    (void)on;
    return 0;
}

/* stub-m2: return 0 without reverb. */
long SpuSetReverbDepth(SpuReverbAttr *attr)
{
    clock_psyq_entry();
    (void)attr;
    return 0;
}

/* stub-m2: return 0 without reverb. */
long SpuSetReverbModeParam(SpuReverbAttr *attr)
{
    clock_psyq_entry();
    (void)attr;
    return 0;
}

/* stub-m2: return 0 without reverb. */
unsigned long SpuSetReverbVoice(long on, unsigned long voice)
{
    clock_psyq_entry();
    (void)on;
    (void)voice;
    return 0;
}

