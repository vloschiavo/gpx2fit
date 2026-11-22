/* gpx2fit: convert a GPX track to a FIT course file. (stub: task 1.1) */
#include <stdio.h>

static void usage(FILE *out, const char *prog)
{
    fprintf(out, "Usage: %s [options] input.gpx output.fit\n", prog);
}

int main(int argc, char **argv)
{
    (void)argc;
    usage(stdout, argv[0]);
    return 0;
}
