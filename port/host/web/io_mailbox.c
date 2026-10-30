/* Single request slot. The mutex owns request fields until the response is consumed. */
#include "io_mailbox.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <stdatomic.h>

/* u32 words: state, offset lo/hi, length, destination, error, size lo/hi, waits, requests.
 * state 0=idle, 1=request, 2=response. Release/acquire publishes fields and bytes. */
static _Atomic uint32_t box[10];
static pthread_mutex_t readers = PTHREAD_MUTEX_INITIALIZER;
EMSCRIPTEN_KEEPALIVE void *io_mailbox_address(void) { return box; }
uint64_t io_size(void) { return (uint64_t)box[6] | (uint64_t)box[7] << 32; }
int io_read(uint64_t offset, void *out, uint32_t length)
{
    pthread_mutex_lock(&readers);
    box[1] = (uint32_t)offset; box[2] = offset >> 32;
    box[3] = length; box[4] = (uintptr_t)out; box[5] = 0;
    atomic_store_explicit(&box[0], 1, memory_order_release);
    emscripten_futex_wake((void *)&box[0], 1);
    while (atomic_load_explicit(&box[0], memory_order_acquire) != 2) {
        box[8]++;
        emscripten_futex_wait((void *)&box[0], 1, __builtin_inf());
    }
    int result = box[5] ? -1 : 0;
    box[9]++;
    atomic_store(&box[0], 0);
    pthread_mutex_unlock(&readers);
    return result;
}
void web_finish(int code)
{
    MAIN_THREAD_EM_ASM({ Module.finished = $0; Module.onFinished?.(); }, code);
    pthread_exit(NULL);
}
