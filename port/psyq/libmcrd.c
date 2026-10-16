#include "psyq.h"
#include "clock.h"

/* Adapted from DW2-PC (CC0): psyq/libmcrd.c @ 816ed0d */
/* Only the async command/result convention is retained; no card storage. */
static unsigned long pending_command;

/* stub-m2: clear the pending command; no card storage. */
void MemCardInit(long flag)
{
    clock_psyq_entry();
    (void)flag;
    pending_command = 0;
}

/* stub-m2: no work; both card slots stay absent. */
void MemCardStart(void)
{
    clock_psyq_entry();
}

/* stub-m2: register McFuncExist and return 1; card absent. */
long MemCardExist(long chan)
{
    clock_psyq_entry();
    (void)chan;
    pending_command = McFuncExist;
    return 1;
}

/* stub-m2: register McFuncAccept and return 1; card absent. */
long MemCardAccept(long chan)
{
    clock_psyq_entry();
    (void)chan;
    pending_command = McFuncAccept;
    return 1;
}

/* stub-m2: register McFuncReadFile and return 1 without reading. */
long MemCardReadFile(long chan, char *name, long *data, long offset, long bytes)
{
    clock_psyq_entry();
    (void)chan;
    (void)name;
    (void)data;
    (void)offset;
    (void)bytes;
    pending_command = McFuncReadFile;
    return 1;
}

/* stub-m2: register McFuncWriteFile and return 1 without writing. */
long MemCardWriteFile(long chan, char *name, long *data, long offset, long bytes)
{
    clock_psyq_entry();
    (void)chan;
    (void)name;
    (void)data;
    (void)offset;
    (void)bytes;
    pending_command = McFuncWriteFile;
    return 1;
}

/* stub-m2: return McErrCardNotExist. */
long MemCardCreateFile(long chan, char *name, long blocks)
{
    clock_psyq_entry();
    (void)chan;
    (void)name;
    (void)blocks;
    return McErrCardNotExist;
}

/* stub-m2: return McErrCardNotExist. */
long MemCardDeleteFile(long chan, char *name)
{
    clock_psyq_entry();
    (void)chan;
    (void)name;
    return McErrCardNotExist;
}

/* stub-m2: return McErrCardNotExist. */
long MemCardFormat(long chan)
{
    clock_psyq_entry();
    (void)chan;
    return McErrCardNotExist;
}

/* stub-m2: return McErrCardNotExist without changing outputs. */
long MemCardGetDirentry(long chan, char *name, struct DIRENTRY *dir, long *files, long offset, long max)
{
    clock_psyq_entry();
    (void)chan;
    (void)name;
    (void)dir;
    (void)files;
    (void)offset;
    (void)max;
    return McErrCardNotExist;
}

/* stub-m2: consume command with McErrCardNotExist (1), or return -1; never wait. */
long MemCardSync(long mode, unsigned long *cmd, unsigned long *result)
{
    clock_psyq_entry();
    (void)mode;
    if (!pending_command) return -1;
    if (cmd) *cmd = pending_command;
    if (result) *result = McErrCardNotExist;
    pending_command = 0;
    return 1;
}

