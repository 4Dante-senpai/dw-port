/* The game publishes complete libmcrd commands; only the main thread persists them. */
#include "card.h"
#include "io_mailbox.h"
#include "../../psyq/card_image.h"
#include "../../psyq/crt.h"
#include <emscripten.h>
#include <emscripten/threading.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

static struct {
    _Atomic uint32_t seq;
    uint8_t image[CARD_IMAGE_SIZE];
} snapshots[2];

static int commit(void *context, const uint8_t *data, size_t size)
{
    int port = (int)(uintptr_t)context;
    atomic_fetch_add(&snapshots[port].seq, 1);
    memcpy(snapshots[port].image, data, size);
    atomic_fetch_add(&snapshots[port].seq, 1);
    MAIN_THREAD_ASYNC_EM_ASM({ Module.cards.committed($0); }, port);
    return 0;
}
EMSCRIPTEN_KEEPALIVE void *web_card_address(int port)
{
    return port >= 0 && port < 2 ? &snapshots[port] : NULL;
}
EMSCRIPTEN_KEEPALIVE void web_card_attach(int port, int blank)
{
    if (port < 0 || port > 1) return;
    if (blank) card_image_new(snapshots[port].image);
    card_image_attach(port, snapshots[port].image, commit, (void *)(uintptr_t)port);
}
void web_card_test(unsigned iterations)
{
    _Atomic uint32_t wait_word = 0;
    uint8_t *data[2] = {card_image_data(0), card_image_data(1)};
    if (!data[0] || !data[1]) web_finish(1);
    for (unsigned n = 1; n <= iterations; n++) {
        for (unsigned port = 0; port < 2; port++) {
            for (unsigned i = 0; i < CARD_IMAGE_SIZE; i++) data[port][i] = (uint8_t)(n + port * 128 + i * 7);
            card_image_commit(port);
        }
        emscripten_futex_wait(&wait_word, 0, 5);
    }
    web_finish(0);
}
