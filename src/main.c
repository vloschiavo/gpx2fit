/* gpx2fit: convert a GPX track to a FIT course file.
 * (tasks 1.4 CLI parsing, 2.x GPX reading; pipeline is wired in 8.1) */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "course.h"
#include "gpx.h"

/* Exit codes. */
enum {
    EXIT_OK    = 0,
    EXIT_ERROR = 1, /* runtime failure (I/O, bad input file); used from 2.x on */
    EXIT_USAGE = 2  /* bad command line */
};

#define DEFAULT_SNAP_M 250.0

/* Parsed command line. String members point into argv (not owned). */
struct options {
    const char *input;     /* input.gpx */
    const char *output;    /* output.fit */
    double      snap_m;    /* max waypoint-to-track distance, meters */
    const char *name;      /* course name override; NULL = from GPX */
    const char *pace;      /* raw --pace text, parsed in task 6.1; NULL = default */
    const char *start;     /* raw --start text, parsed in task 6.1; NULL = default */
    bool        quiet;     /* suppress everything except errors */
    bool        strict;    /* treat warnings as errors */
    long        max_points; /* decimation target; 0 = no limit */
    long        track;     /* 1-based <trk> to use; 0 = longest */
    const char *map_file;  /* user type-map file; NULL = none */
    bool        help;      /* --help given */
    bool        dump;      /* hidden --dump-options, for tests */
};

static void usage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options] input.gpx output.fit\n"
        "\n"
        "Convert a GPX track and waypoints to a FIT course file.\n"
        "\n"
        "Options:\n"
        "  --snap METERS      max distance from a waypoint to the track\n"
        "                     (default %.0f)\n"
        "  --name TEXT        course name (default: GPX track name)\n"
        "  --pace PACE        synthetic pace, m/s or min/km (e.g. 2.5, 6:40)\n"
        "  --start TIME       synthetic start time (ISO 8601, UTC)\n"
        "  --track N          use the Nth <trk> of the GPX file (default: longest)\n"
        "  --max-points N     reduce the track to at most N points (N >= 2)\n"
        "  --map FILE         waypoint name-to-type rules, tried before defaults\n"
        "  --strict           treat warnings as errors\n"
        "  -q, --quiet        print only errors\n"
        "  -h, --help         show this help and exit\n"
        "\n"
        "Exit status: 0 success, 1 error, 2 bad usage.\n",
        prog, DEFAULT_SNAP_M);
}

/* Parse a finite, non-negative double. Returns 0 on success, -1 on error. */
static int parse_nonneg_double(const char *s, double *out)
{
    char *end;
    double v;

    errno = 0;
    v = strtod(s, &end);
    if (end == s || *end != '\0' || errno == ERANGE || !isfinite(v) || v < 0.0)
        return -1;
    *out = v;
    return 0;
}

/* Parse a base-10 long. Returns 0 on success, -1 on error. */
static int parse_long(const char *s, long *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE)
        return -1;
    *out = v;
    return 0;
}

/* Parse argv into *o. Returns EXIT_OK, or EXIT_USAGE after printing a
 * message to stderr. With --help, returns EXIT_OK and sets o->help. */
static int parse_options(int argc, char **argv, struct options *o)
{
    enum { OPT_SNAP = 256, OPT_NAME, OPT_PACE, OPT_START, OPT_STRICT,
           OPT_MAX_POINTS, OPT_MAP, OPT_TRACK, OPT_DUMP };
    static const struct option longopts[] = {
        { "snap",         required_argument, NULL, OPT_SNAP },
        { "name",         required_argument, NULL, OPT_NAME },
        { "pace",         required_argument, NULL, OPT_PACE },
        { "start",        required_argument, NULL, OPT_START },
        { "quiet",        no_argument,       NULL, 'q' },
        { "strict",       no_argument,       NULL, OPT_STRICT },
        { "max-points",   required_argument, NULL, OPT_MAX_POINTS },
        { "map",          required_argument, NULL, OPT_MAP },
        { "track",        required_argument, NULL, OPT_TRACK },
        { "help",         no_argument,       NULL, 'h' },
        { "dump-options", no_argument,       NULL, OPT_DUMP }, /* hidden */
        { NULL, 0, NULL, 0 }
    };
    const char *prog = argv[0];
    int c;

    memset(o, 0, sizeof *o);
    o->snap_m = DEFAULT_SNAP_M;

    optind = 1; /* allow parse_options to be called more than once */
    while ((c = getopt_long(argc, argv, "qh", longopts, NULL)) != -1) {
        switch (c) {
        case OPT_SNAP:
            if (parse_nonneg_double(optarg, &o->snap_m) != 0) {
                fprintf(stderr, "%s: invalid --snap value '%s' "
                        "(need a number >= 0)\n", prog, optarg);
                return EXIT_USAGE;
            }
            break;
        case OPT_NAME:   o->name = optarg;  break;
        case OPT_PACE:   o->pace = optarg;  break;
        case OPT_START:  o->start = optarg; break;
        case OPT_MAP:    o->map_file = optarg; break;
        case OPT_STRICT: o->strict = true;  break;
        case OPT_MAX_POINTS:
            if (parse_long(optarg, &o->max_points) != 0 || o->max_points < 2) {
                fprintf(stderr, "%s: invalid --max-points value '%s' "
                        "(need an integer >= 2)\n", prog, optarg);
                return EXIT_USAGE;
            }
            break;
        case OPT_TRACK:
            if (parse_long(optarg, &o->track) != 0 || o->track < 1) {
                fprintf(stderr, "%s: invalid --track value '%s' "
                        "(need an integer >= 1)\n", prog, optarg);
                return EXIT_USAGE;
            }
            break;
        case 'q': o->quiet = true; break;
        case 'h': o->help = true;  return EXIT_OK;
        case OPT_DUMP: o->dump = true; break;
        default: /* getopt_long already printed the reason */
            fprintf(stderr, "Try '%s --help' for more information.\n", prog);
            return EXIT_USAGE;
        }
    }

    if (argc - optind != 2) {
        fprintf(stderr, "%s: expected exactly 2 arguments "
                "(input.gpx output.fit), got %d\n", prog, argc - optind);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog);
        return EXIT_USAGE;
    }
    o->input  = argv[optind];
    o->output = argv[optind + 1];
    return EXIT_OK;
}

static void dump_options(const struct options *o)
{
    printf("input=%s\n",      o->input);
    printf("output=%s\n",     o->output);
    printf("snap=%g\n",       o->snap_m);
    printf("name=%s\n",       o->name ? o->name : "(none)");
    printf("pace=%s\n",       o->pace ? o->pace : "(none)");
    printf("start=%s\n",      o->start ? o->start : "(none)");
    printf("max_points=%ld\n", o->max_points);
    printf("track=%ld\n",     o->track);
    printf("map=%s\n",        o->map_file ? o->map_file : "(none)");
    printf("quiet=%d\n",      o->quiet);
    printf("strict=%d\n",     o->strict);
}

int main(int argc, char **argv)
{
    struct options opt;
    int rc = parse_options(argc, argv, &opt);

    if (rc != EXIT_OK)
        return rc;
    if (opt.help) {
        usage(stdout, argv[0]);
        return EXIT_OK;
    }
    if (opt.dump) {
        dump_options(&opt);
        return EXIT_OK;
    }

    /* Temporary driver until the pipeline is wired up in task 8.1. */
    struct course c;
    struct gpx_options go = { .track = opt.track, .quiet = opt.quiet };
    char err[256];

    course_init(&c);
    if (read_gpx(opt.input, &c, &go, err, sizeof err) != 0) {
        fprintf(stderr, "%s: error: %s\n", argv[0], err);
        course_free(&c);
        return EXIT_ERROR;
    }
    if (opt.name) {
        char *n = strdup(opt.name);
        if (!n) {
            fprintf(stderr, "%s: error: out of memory\n", argv[0]);
            course_free(&c);
            return EXIT_ERROR;
        }
        free(c.name);
        c.name = n;
    }
    if (!opt.quiet)
        printf("Read '%s': %zu trackpoints, %zu waypoints\n",
               c.name, c.n_tp, c.n_wp);

    course_free(&c);
    return EXIT_OK;
}
