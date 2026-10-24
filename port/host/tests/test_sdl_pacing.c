/* Exercise the real VSync(n), SPU and SDL host without copyrighted disc data. */
#include "../../psyq/psyq.h"
int dw1_game_main(void)
{
    ResetCallback();
    /* Frames of 1, 2 and 3 VBlanks (VSync(0) waits one; VSync(1) would not wait): wall time must follow the virtual VBlanks. */
    for (;;) { VSync(0); VSync(2); VSync(3); }
    return 0;
}
