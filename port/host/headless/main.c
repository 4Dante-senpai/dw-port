/* Native headless entry point; parser and startup are shared with SDL. */
#include "../native_cli.h"
int main(int argc, char **argv)
{
    NativeCli options;
    int result = native_cli_parse(argc, argv, &options);
    return result ? result : native_cli_run(&options);
}
