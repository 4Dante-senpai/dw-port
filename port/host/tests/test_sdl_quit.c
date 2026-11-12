/* Exercise the actual main-loop hook with a synthetic game that asks to quit. */
#define main sdl_host_main
#include "../sdl/main.c"
#undef main
#include "../../psyq/clock.h"
#include "../../backend/gpu.h"
#include <sys/wait.h>
#include <unistd.h>
extern void port_pad_latch(uint16_t mask);
int dw1_game_main(void)
{
    if (recording_session) {
        gpu_reset(1);
        gpu_set_display(0, 0, 2, 2, 0);
        gpu_set_display_enable(1);
        port_vsync_return();
        port_pad_latch(8);
        for (unsigned k = 1;; k++) {
            clock_advance(clock_vblank_cycle(1));
            if (k == 3) {
                SDL_Event event = {.type = SDL_EVENT_QUIT};
                if (!SDL_PushEvent(&event)) exit(1);
            }
            SDL_Delay(1);
        }
    }
    SDL_Event event = {.type = SDL_EVENT_QUIT};
    if (!SDL_PushEvent(&event)) exit(1);
    for (;;) pause();
}
int main(int argc, char **argv)
{
    if (argc > 1) return sdl_host_main(argc, argv);
    char directory[] = "/tmp/dw1-sdl-quit-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    char record[128], digest[128];
    snprintf(record, sizeof(record), "%s/inputs.txt", directory);
    snprintf(digest, sizeof(digest), "%s/digest.txt", directory);
    for (int observed = 0; observed < 2; observed++) {
        pid_t child = fork();
        if (child < 0) return 1;
        if (!child) {
            if (observed) execl(argv[0], argv[0], "--frames", "1000000", "--record-inputs", record,
                                "--digest", digest, "--digest-every", "1000000", (char *)NULL);
            else execl(argv[0], argv[0], "--frames", "1000000", (char *)NULL);
            _exit(1);
        }
        int status;
        if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    }
    FILE *file = fopen(record, "r");
    char line[256];
    if (!file || !fgets(line, sizeof(line), file) || strcmp(line, "0 0008\n") ||
        !fgets(line, sizeof(line), file) || strcmp(line, "1 0000\n") || fgetc(file) != EOF) return 1;
    fclose(file);
    file = fopen(digest, "r");
    unsigned k, overlay;
    char display[65], hash[65];
    if (!file || fscanf(file, "%u %u %64s %64s", &k, &overlay, display, hash) != 4 || k < 3 ||
        strlen(display) != 64 || strlen(hash) != 64 || fgetc(file) != '\n' || fgetc(file) != EOF) return 1;
    fclose(file);
    unlink(record); unlink(digest); rmdir(directory);
    return 0;
}
