#ifndef PORT_NATIVE_CLI_H
#define PORT_NATIVE_CLI_H
#include "../psyq/crt.h"
typedef struct {
    long frames;
    int until_overlay, headless;
    const char *pack, *cards[2];
    PortCapture capture;
} NativeCli;
int native_cli_parse(int argc, char **argv, NativeCli *options);
int native_cli_run(const NativeCli *options);
#endif
