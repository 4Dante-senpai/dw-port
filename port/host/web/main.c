/* Development web host. Storage belongs exclusively to the I/O worker. */
#include "../../psyq/libpress.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <emscripten.h>
#include "io_mailbox.h"
#include "../../backend/cdrom.h"
#include "../../psyq/libgte.h"
#include "../pack/pack.h"
#include <string.h>
#include "../../psyq/crt.h"
int plat_pattern;
int plat_validation;
static const char *pack_path;
static int io_test;
static int io_guard_test;
/* Synchronously record each guard hit on the main thread before throwing. */
EMSCRIPTEN_KEEPALIVE void web_storage_guard_hit(void)
{
    MAIN_THREAD_EM_ASM({
        Module.storageAccesses++;
        Module.storageAccessThreads.push($0);
    }, (uintptr_t)pthread_self());
}
static void *reader(void *arg)
{
    unsigned id = (uintptr_t)arg;
    if (io_guard_test && !EM_ASM_INT({
        try { void navigator.storage; } catch (error) { return 1; }
        return 0;
    })) return (void *)1;
    Pack *p;
    char error[256];
    p = pack_open("dw1/us.pack", NULL, error, sizeof(error));
    if (!p) return (void *)1;
    const PackEntry *entry = pack_find_path(p, "\\SLUS_010.32;1");
    unsigned char bytes[113];
    for (unsigned j = 0; j < 20; j++) {
        unsigned off = (id * 1173 + j * 419) % 4800;
        if (!entry || pack_read(p, entry, off, bytes, sizeof(bytes))) return (void *)1;
        for (unsigned k = 0; k < sizeof(bytes); k++) {
            unsigned pos = off + k;
            if (bytes[k] != (unsigned char)((22 + pos / 2048) * 31 +
                                          (24 + pos % 2048) * 7)) {
                fprintf(stderr, "io: different bytes at offset %u\n", pos);
                return (void *)1;
            }
        }
    }
    if (pack_read(p, entry, entry->size, bytes, 1) != -1 ||
        io_read(io_size(), bytes, 1) != -1 ||
        io_read(UINT64_MAX, bytes, 1) != -1) return (void *)1;
    pack_close(p);
    return NULL;
}
int web_prepare(void)
{
    if (io_test) {
        pthread_t threads[4];
        for (unsigned i = 0; i < 4; i++)
            if (pthread_create(&threads[i], NULL, reader, (void *)(uintptr_t)i)) web_finish(1);
        int failed = 0;
        for (unsigned i = 0; i < 4; i++) {
            void *result;
            pthread_join(threads[i], &result);
            failed |= result != NULL;
        }
        MAIN_THREAD_EM_ASM({
            const box = new Uint32Array(HEAPU8.buffer, $0, 10);
            Module.ioResult = ({failed: $1, waits: box[8], requests: box[9]});
        }, io_mailbox_address(), failed);
        web_finish(failed);
    }
    if (pack_path) {
        char error[1024];
        if (cdrom_open(pack_path, DW1_MANIFEST_SHA256, error, sizeof(error)) ||
            libgte_load_tables(error, sizeof(error)) || libpress_load_tables(error, sizeof(error))) {
            MAIN_THREAD_EM_ASM({Module.printErr(UTF8ToString($0));}, error);
            return -1;
        }
    }
    return 0;
}
int main(int argc, char **argv)
{
    long frames = 1;
    PortCapture config = {0};
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pack") && i + 1 < argc) pack_path = argv[++i];
        else if (!strcmp(argv[i], "--io-test")) io_test = 1;
        else if (!strcmp(argv[i], "--io-guard-test")) io_test = io_guard_test = 1;
        else if (!strcmp(argv[i], "--capture-mark") && i + 1 < argc) config.mark = argv[++i];
        else if (!strcmp(argv[i], "--capture-vblank") && i + 1 < argc) config.vblank = strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--pattern")) plat_pattern = 1;
        else if (!strcmp(argv[i], "--validate")) plat_validation = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            frames = strtol(argv[++i], &end, 10);
            if (*end || frames < 0) return 2;
        } else return 2;
    }
    config.capture = config.mark ? "capture.ppm" : NULL;
    return port_run_game(frames, 0, &config);
}
