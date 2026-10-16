#include "psyq.h"
#include "clock.h"

#include <string.h>

static void (*out_callback)();
static int out_pending, out_draining, pending_size;
static unsigned long *pending_buf;

/* stub-m2: clear the callback and pending output. */
void DecDCTReset(int mode)
{
    clock_psyq_entry();
    (void)mode;
    out_callback = NULL;
    out_pending = 0;
}

/* stub-m2: accept input without decoding. */
void DecDCTin(unsigned long *buf, int mode)
{
    clock_psyq_entry();
    (void)buf;
    (void)mode;
}

/* stub-m2: write an empty run-level header and return 0. */
int DecDCTvlc2(unsigned long *bs, unsigned long *buf, unsigned short *table)
{
    clock_psyq_entry();
    (void)bs;
    (void)table;
    buf[0] = 0;
    return 0;
}

/* stub-m2: no VLC table needed for synthetic video. */
void DecDCTvlcBuild(unsigned short *table)
{
    clock_psyq_entry();
    (void)table;
}

/* stub-m2: install or remove the output callback; return 0. */
int DecDCToutCallback(void (*func)())
{
    clock_psyq_entry();
    out_callback = func;
    return 0;
}

/* stub-m2: zero size longs; drain reentrant callbacks iteratively on the game thread. */
void DecDCTout(unsigned long *buf, int size)
{
    clock_psyq_entry();
    pending_buf = buf;
    pending_size = size;
    out_pending = 1;
    if (out_draining) return;
    out_draining = 1;
    while (out_pending) {
        unsigned long *dst = pending_buf;
        int words = pending_size;
        out_pending = 0;
        if (words > 0) memset(dst, 0, (size_t)words * sizeof(*dst));
        if (out_callback) out_callback();
    }
    out_draining = 0;
}

