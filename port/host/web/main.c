/* Development host; no Pack or browser storage is accessed in this story. */
#include <stdlib.h>
#include <string.h>
#include "../../psyq/crt.h"
int plat_pattern;
int plat_validation;
int main(int argc, char **argv)
{
    long frames = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--pattern")) plat_pattern = 1;
        else if (!strcmp(argv[i], "--validate")) plat_validation = 1;
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char *end;
            frames = strtol(argv[++i], &end, 10);
            if (*end || frames < 1) return 2;
        } else return 2;
    }
    return port_run_game(frames, 0, NULL);
}
