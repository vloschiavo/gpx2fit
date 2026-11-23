/* gpx2fit: convert a GPX track to a FIT course file.
 * (tasks 1.4 CLI parsing, 2.x GPX reading, 4.x snapping, 5.4 --map and name
 * handling, 6.x synthetic times, 7.x FIT output, 8.1 pipeline, 8.2
 * --max-points, 8.3 summary) */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "course.h"
#include "decimate.h"
#include "fit_write.h"
#include "geo.h"
#include "gpx.h"
#include "names.h"
#include "snap.h"
#include "sport.h"
#include "symmap.h"
#include "timing.h"

/* Exit codes. */
enum {
    EXIT_OK    = 0,
    EXIT_ERROR = 1, /* runtime failure (I/O, bad input file); used from 2.x on */
    EXIT_USAGE = 2  /* bad command line */
};

#define DEFAULT_SNAP_M 250.0

/* Elevation changes smaller than this are treated as noise in the summary's
 * ascent and descent (see course_elevation_stats). */
#define ELE_THRESHOLD_M 1.0

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
    enum snap_mode pass;   /* which pass of the track a waypoint snaps to */
    const char *map_file;  /* user type-map file; NULL = none */
    const char *sport;     /* raw --sport text; NULL = default (hiking) */
    uint8_t     sport_id;  /* course.sport, resolved by sport_parse() */
    uint8_t     sub_sport; /* course.sub_sport, resolved by sport_parse() */
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
        "  --pass MODE        which pass of the track a waypoint snaps to when the\n"
        "                     track visits it more than once: nearest (default),\n"
        "                     ordered, first, last\n"
        "  --name TEXT        course name (default: GPX track name)\n"
        "  --sport TYPE       activity type: hiking (default), running,\n"
        "                     trail-running, cycling, road-cycling, gravel, mtb,\n"
        "                     mountain-biking, other (aliases; case-insensitive)\n"
        "  --pace PACE        synthetic pace, m/s or min/km (e.g. 2.5, 6:40;\n"
        "                     default 5 km/h)\n"
        "  --start TIME       synthetic start time, UTC: YYYY-MM-DD or\n"
        "                     YYYY-MM-DDTHH:MM:SSZ (default 2025-01-01T00:00:00Z)\n"
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
    enum { OPT_SNAP = 256, OPT_NAME, OPT_SPORT, OPT_PACE, OPT_START,
           OPT_STRICT, OPT_MAX_POINTS, OPT_MAP, OPT_TRACK, OPT_PASS, OPT_DUMP };
    static const struct option longopts[] = {
        { "snap",         required_argument, NULL, OPT_SNAP },
        { "name",         required_argument, NULL, OPT_NAME },
        { "sport",        required_argument, NULL, OPT_SPORT },
        { "pace",         required_argument, NULL, OPT_PACE },
        { "start",        required_argument, NULL, OPT_START },
        { "quiet",        no_argument,       NULL, 'q' },
        { "strict",       no_argument,       NULL, OPT_STRICT },
        { "max-points",   required_argument, NULL, OPT_MAX_POINTS },
        { "map",          required_argument, NULL, OPT_MAP },
        { "track",        required_argument, NULL, OPT_TRACK },
        { "pass",         required_argument, NULL, OPT_PASS },
        { "help",         no_argument,       NULL, 'h' },
        { "dump-options", no_argument,       NULL, OPT_DUMP }, /* hidden */
        { NULL, 0, NULL, 0 }
    };
    const char *prog = argv[0];
    int c;

    memset(o, 0, sizeof *o);
    o->snap_m = DEFAULT_SNAP_M;
    o->pass = SNAP_NEAREST;
    o->sport_id = FIT_SPORT_HIKING;
    o->sub_sport = FIT_SUB_SPORT_GENERIC;

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
        case OPT_SPORT:
            /* Resolve now: an unknown activity is a usage error (exit 2)
             * before any work, like a bad --pace or --start. */
            if (!sport_parse(optarg, &o->sport_id, &o->sub_sport)) {
                fprintf(stderr, "%s: invalid --sport value '%s' (need one of "
                        "hiking, running, trail-running, cycling, "
                        "road-cycling, gravel, mtb, mountain-biking, "
                        "other)\n", prog, optarg);
                return EXIT_USAGE;
            }
            o->sport = optarg;
            break;
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
        case OPT_PASS:
            if (snap_mode_parse(optarg, &o->pass) != 0) {
                fprintf(stderr, "%s: invalid --pass value '%s' (need one of "
                        "nearest, ordered, first, last)\n", prog, optarg);
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
    printf("pass=%s\n",      snap_mode_name(o->pass));
    printf("map=%s\n",        o->map_file ? o->map_file : "(none)");
    printf("sport=%u\n",      (unsigned)o->sport_id);
    printf("sub_sport=%u\n",  (unsigned)o->sub_sport);
    printf("quiet=%d\n",      o->quiet);
    printf("strict=%d\n",     o->strict);
}

/* Warning from the map parser (task 5.4). ctx is the struct options. */
static void map_warn(const char *source, int line, const char *msg, void *ctx)
{
    const struct options *o = ctx;

    if (!o->quiet)
        fprintf(stderr, "warning: %s:%d: %s\n", source, line, msg);
}

/* The track line of the summary (8.3). total_m is the full course length,
 * es comes from the full-resolution track, n_full is the point count before
 * any --max-points reduction. */
static void print_track_summary(const struct course *c,
                                const struct ele_stats *es,
                                double total_m, size_t n_full)
{
    printf("Track: %.2f km, ", total_m / 1000.0);
    if (es->valid)
        printf("ascent %.0f m, descent %.0f m, ", es->ascent, es->descent);
    else
        printf("no elevation data, ");
    printf("%zu points", c->n_tp);
    if (c->n_tp != n_full)
        printf(" (reduced from %zu)", n_full);
    printf("\n");
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

    /* Check --pace and --start now, so a bad value is a usage error (exit 2)
     * before any work is done. The raw text stays in opt for --dump-options. */
    double   pace_mps;
    uint32_t start_fit;
    char     verr[128];

    if (parse_pace(opt.pace, &pace_mps, verr, sizeof verr) != 0) {
        fprintf(stderr, "%s: invalid --pace value '%s' (%s)\n",
                argv[0], opt.pace, verr);
        return EXIT_USAGE;
    }
    if (parse_start(opt.start, &start_fit, verr, sizeof verr) != 0) {
        fprintf(stderr, "%s: invalid --start value '%s' (%s)\n",
                argv[0], opt.start, verr);
        return EXIT_USAGE;
    }

    /* The pipeline (8.1): read GPX, distances, snap, point types, synthetic
     * times, write FIT. Each stage reports failure and stops with exit 1; the
     * output file is only written once every earlier stage has succeeded.
     * Normal output (the summary) is printed after the file is written, so
     * that it is never shown for a run that then fails. */
    struct course c;
    struct symmap map;
    struct snap_report sr;
    struct ele_stats es;
    size_t n_full;
    double total_m;
    int trc;
    struct gpx_options go = { .track = opt.track, .quiet = opt.quiet };
    char err[256];

    course_init(&c);
    symmap_init(&map);
    rc = EXIT_ERROR;

    /* Load the type map first, so a bad --map file fails before any work.
     * User rules come before the embedded defaults. */
    if (symmap_load(&map, opt.map_file, map_warn, &opt, err, sizeof err) != 0) {
        fprintf(stderr, "%s: error: %s\n", argv[0], err);
        goto out;
    }

    if (read_gpx(opt.input, &c, &go, err, sizeof err) != 0) {
        fprintf(stderr, "%s: error: %s\n", argv[0], err);
        goto out;
    }
    if (opt.name) {
        char *n = strdup(opt.name);
        if (!n) {
            fprintf(stderr, "%s: error: out of memory\n", argv[0]);
            goto out;
        }
        free(c.name);
        c.name = n;
    }
    if (c.name && strlen(c.name) > COURSE_NAME_MAX) {
        if (!opt.quiet)
            fprintf(stderr, "warning: course name is longer than %d bytes, "
                    "truncating\n", COURSE_NAME_MAX);
        utf8_truncate(c.name, strlen(c.name) + 1, c.name, COURSE_NAME_MAX);
    }
    if (!opt.quiet)
        printf("Read '%s': %zu trackpoints, %zu waypoints\n",
               c.name, c.n_tp, c.n_wp);

    course_compute_distances(&c);
    {
        struct snap_options so = { opt.pass, opt.snap_m, opt.quiet, opt.strict };
        int src = snap_waypoints(&c, &so, &sr);

        if (src == SNAP_NOMEM) {
            fprintf(stderr, "%s: error: out of memory\n", argv[0]);
            goto out;
        }
        if (src == SNAP_STRICT) {
            /* The run fails, so the normal summary will not be printed;
             * show which waypoints were snapped and which were skipped. */
            if (!opt.quiet)
                snap_print_summary(stdout, &c, &sr);
            fprintf(stderr, "%s: error: --strict: %zu waypoint(s) farther than "
                    "%g m from the track\n", argv[0], sr.n_skipped, opt.snap_m);
            goto out;
        }
    }

    /* Point types from names, then name truncation (5.4, 5.5). */
    course_apply_types(&c, &map, opt.quiet);

    /* Synthetic times for the track, then for the course points (6.1, 6.2).
     * Before any --max-points decimation (8.2), which must not change them. */
    trc = course_assign_times(&c, start_fit, pace_mps);
    if (trc != TIMING_OK) {
        fprintf(stderr, "%s: error: %s\n", argv[0], timing_strerror(trc));
        goto out;
    }
    course_assign_cp_times(&c);

    /* Summary figures come from the full-resolution track, so take them
     * before --max-points changes it (8.2, 8.3). */
    n_full = c.n_tp;
    total_m = c.n_tp ? c.tp[c.n_tp - 1].dist : 0.0;
    course_elevation_stats(&c, ELE_THRESHOLD_M, &es);

    /* --max-points (8.2). Course points refer to the track by distance and
     * the surviving points keep their distances and times, so nothing else
     * needs updating. */
    if (opt.max_points > 0) {
        struct decimate_report dr;

        if (course_decimate(&c, (size_t)opt.max_points, &dr) != 0) {
            fprintf(stderr, "%s: error: out of memory\n", argv[0]);
            goto out;
        }
        if (dr.exceeded) {
            if (opt.strict) {
                fprintf(stderr, "%s: error: --strict: --max-points %ld is "
                        "less than the %zu points that must be kept (first, "
                        "last, and one per course point)\n",
                        argv[0], opt.max_points, dr.n_required);
                goto out;
            }
            if (!opt.quiet)
                fprintf(stderr, "warning: --max-points %ld is less than the "
                        "%zu points that must be kept (first, last, and one "
                        "per course point); keeping %zu\n",
                        opt.max_points, dr.n_required, dr.n_after);
        }
    }

    /* Encode and write the FIT course file (7.x). */
    {
        struct fit_options fo;
        int frc;

        fit_options_default(&fo);
        fo.sport = opt.sport_id;
        fo.sub_sport = opt.sub_sport;
        frc = fit_write_course(&c, &fo, opt.output);
        if (frc != FIT_OK) {
            fprintf(stderr, "%s: error: cannot write '%s': %s\n",
                    argv[0], opt.output, fit_strerror(frc));
            goto out;
        }
    }

    /* The summary (8.3): chosen activity, track totals, one line per course
     * point, the snapped tally, then the output file. Nothing under --quiet. */
    if (!opt.quiet) {
        printf("Activity: %s\n", opt.sport ? opt.sport : "hiking");
        print_track_summary(&c, &es, total_m, n_full);
        snap_print_summary(stdout, &c, &sr);
        printf("Wrote '%s' (%zu course points)\n", opt.output, c.n_cp);
    }

    rc = EXIT_OK;
out:
    symmap_free(&map);
    course_free(&c);
    return rc;
}
