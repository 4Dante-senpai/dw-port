/* Blocking game-loop model for the web threading options (docs/research/web-constraints.md):
 * a VSync() that blocks for one 59.94 Hz period and a longjmp out of a nested call, like the
 * game's main loop does. Built three ways by run.sh. */
#include <setjmp.h>
#include <stdio.h>
#include <emscripten.h>
#include <emscripten/em_math.h>

#ifdef MODEL_THREAD
#include <time.h>
static void block_ms(double ms) {
    struct timespec ts = { 0, (long)(ms * 1e6) };
    nanosleep(&ts, NULL);
}
#else
static void block_ms(double ms) { emscripten_sleep((unsigned)ms); }
#endif

static jmp_buf env;
static volatile int sink;

static void deep(int n) {
    if (n == 0) longjmp(env, 1);
    sink += n;
    deep(n - 1);
}

static void VSync(void) { block_ms(1001.0 / 60.0); }

int main(void) {
    double t0 = emscripten_get_now();
    int frames, jumps = 0;
    for (frames = 0; frames < 60; frames++) {
        if (setjmp(env) == 0) deep(8); else jumps++;
        VSync();
    }
    printf("frames=%d longjmps=%d elapsed_ms=%.0f\n", frames, jumps, emscripten_get_now() - t0);
    return 0;
}
