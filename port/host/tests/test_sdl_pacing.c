/* Exercise the real VSync(n), SPU and SDL host without copyrighted disc data. */
#include "../../psyq/psyq.h"
#include <stdlib.h>
#include <time.h>
int dw1_game_main(void)
{
    ResetCallback();
    /* Frames of 1, 2 and 3 VBlanks (VSync(0) waits one; VSync(1) would not wait): wall time must follow the virtual VBlanks. */
    for (unsigned iteration = 0;; iteration++) {
        if (iteration == 5 && getenv("DW1_TEST_HOST_STALL")) {
            /* A real host pause exceeds four VBlanks without advancing game time. */
            struct timespec pause = { 0, 150000000 };
            nanosleep(&pause, NULL);
        }
        VSync(0); VSync(2); VSync(3);
    }
    return 0;
}
