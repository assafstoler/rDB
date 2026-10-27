/* Tiny subprocesses for testing the legacy runner, not library functionality. */
#include <signal.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    puts(strcmp(argv[1], "mismatch") == 0 ? "WRONG" : "OK");
    fflush(stdout);
    if (strcmp(argv[1], "nonzero") == 0) return 23;
    if (strcmp(argv[1], "stderr") == 0) fputs("unexpected diagnostic\n", stderr);
    if (strcmp(argv[1], "signal") == 0) raise(SIGTERM);
    return 0;
}
