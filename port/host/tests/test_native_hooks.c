/* Verify thread ownership and scene precedence through the real CRT observer. */
#include "../plat.h"
#include "../../psyq/clock.h"
#include "../../psyq/crt.h"
#include <assert.h>
#include <stdlib.h>
static pthread_t main_thread;
static int initialized, latched, scene_inputs;
void plat_game_init(void)
{
    assert(!pthread_equal(pthread_self(), main_thread)); initialized++;
}
void plat_input_vblank(void) { latched++; }
int plat_main_loop(pthread_t game)
{
    assert(pthread_equal(pthread_self(), main_thread));
    return pthread_join(game, NULL);
}
int dw1_game_main(void)
{
    clock_advance(clock_vblank_cycle(3));
    assert(initialized == 1 && latched == (scene_inputs ? 0 : 3));
    exit(0);
}
int main(int argc, char **argv)
{
    main_thread = pthread_self();
    PortCapture config = {0};
    scene_inputs = argc > 1;
    if (scene_inputs) config.inputs = argv[1];
    return port_run_game(0, 0, &config);
}
