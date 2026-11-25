/* Unit tests for the GPX reader (tasks 2.2-2.6, T6). Run from the repo root. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/course.h"
#include "../src/gpx.h"

#define DATA "tests/data/"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_STR(got, want)                                                 \
    do {                                                                     \
        const char *g_ = (got), *w_ = (want);                                \
        if (!g_ || strcmp(g_, w_) != 0) {                                    \
            printf("FAIL %s:%d: got '%s', want '%s'\n", __FILE__, __LINE__,  \
                   g_ ? g_ : "(null)", w_);                                  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(got, want)                                                \
    CHECK(fabs((got) - (want)) < 1e-9)

/* Read fixture `file`; returns read_gpx's result, message in err. */
static int load_opts(const char *file, struct course *c, long track,
                     bool stitch, char *err, size_t errsz)
{
    char path[256];
    struct gpx_options o = { .track = track, .stitch = stitch, .quiet = true };
    snprintf(path, sizeof path, DATA "%s", file);
    course_init(c);
    return read_gpx(path, c, &o, err, errsz);
}

static int load(const char *file, struct course *c, long track, char *err,
                size_t errsz)
{
    return load_opts(file, c, track, false, err, errsz);
}

/* Expect failure whose message contains `needle`. */
static void expect_error(const char *file, const char *needle)
{
    struct course c;
    char err[256];
    int rc = load(file, &c, 0, err, sizeof err);
    if (rc == 0 || strstr(err, needle) == NULL) {
        printf("FAIL %s: rc=%d, message '%s', wanted one containing '%s'\n",
               file, rc, err, needle);
        failures++;
    }
    course_free(&c);
}

static void test_names(void)
{
    CHECK_STR(gpx_local_name("trkpt"), "trkpt");
    CHECK_STR(gpx_local_name("gpx:trkpt"), "trkpt");
    CHECK_STR(gpx_local_name("http://www.topografix.com/GPX/1/1 trkpt"), "trkpt");
    CHECK_STR(gpx_local_name("{http://www.topografix.com/GPX/1/1}trkpt"), "trkpt");

    CHECK(gpx_ns_ok("trkpt"));
    CHECK(gpx_ns_ok("http://www.topografix.com/GPX/1/0 trkpt"));
    CHECK(gpx_ns_ok("http://www.topografix.com/GPX/1/1 trkpt"));
    CHECK(!gpx_ns_ok("http://www.garmin.com/xmlschemas/TrackPointExtension/v1 hr"));
}

static void test_gpx11(void)
{
    struct course c;
    char err[256];

    CHECK(load("gpx11_basic.gpx", &c, 0, err, sizeof err) == 0);
    CHECK_STR(c.name, "Basic Track"); /* not <metadata><name> */
    CHECK(c.n_tp == 3);
    CHECK_NEAR(c.tp[0].lat, 38.5816);
    CHECK_NEAR(c.tp[0].lon, -121.4944);
    CHECK(c.tp[0].has_ele && fabs(c.tp[0].ele - 10.5) < 1e-9);
    CHECK(!c.tp[1].has_ele); /* only a Garmin extension, no <ele> */
    CHECK(c.tp[2].has_ele && fabs(c.tp[2].ele - 12.25) < 1e-9);
    CHECK(c.n_wp == 2);
    CHECK_STR(c.wp[0].name, "Water");
    CHECK_NEAR(c.wp[0].lat, 38.5820);
    CHECK(c.wp[1].name == NULL); /* unnamed waypoint */
    course_free(&c);
}

static void test_gpx10(void)
{
    struct course c;
    char err[256];

    CHECK(load("gpx10_basic.gpx", &c, 0, err, sizeof err) == 0);
    CHECK_STR(c.name, "Old Format"); /* not the file-level <name> */
    CHECK(c.n_tp == 3);
    CHECK(c.tp[2].has_ele && c.tp[2].ele == 3.0);
    CHECK(c.n_wp == 1);
    CHECK_STR(c.wp[0].name, "Camp");
    course_free(&c);
}

static void test_prefixed(void)
{
    struct course c;
    char err[256];

    CHECK(load("prefixed_ns.gpx", &c, 0, err, sizeof err) == 0);
    CHECK_STR(c.name, "Prefixed Track");
    CHECK(c.n_tp == 2 && c.tp[1].has_ele && c.tp[1].ele == 8.0);
    CHECK(c.n_wp == 1);
    CHECK_STR(c.wp[0].name, "Prefixed WP");
    course_free(&c);
}

static void test_no_ele(void)
{
    struct course c;
    char err[256];

    CHECK(load("no_ele.gpx", &c, 0, err, sizeof err) == 0);
    CHECK(c.n_tp == 3);
    for (size_t i = 0; i < c.n_tp; i++)
        CHECK(!c.tp[i].has_ele); /* absent and empty <ele/> alike */
    CHECK_STR(c.name, "no_ele"); /* fallback: file name without extension */
    CHECK(c.n_wp == 0);
    course_free(&c);
}

static void test_unicode(void)
{
    struct course c;
    char err[256];

    CHECK(load("unicode_wpt.gpx", &c, 0, err, sizeof err) == 0);
    CHECK_STR(c.wp[0].name, "Caf\xC3\xA9 \xE2\x98\x95 \xE6\xB0\xB4");
    CHECK_STR(c.name, "\xC3\x9Cn\xC3\xAF" "c\xC3\xB6" "d\xC3\xA9 Trail");
    course_free(&c);
}

static void test_segments(void)
{
    struct course c;
    char err[256];

    CHECK(load("two_segments.gpx", &c, 0, err, sizeof err) == 0);
    CHECK(c.n_tp == 4);
    CHECK_NEAR(c.tp[1].lat, 1.1);
    CHECK_NEAR(c.tp[2].lat, 2.0); /* second segment follows the first */
    CHECK_NEAR(c.tp[3].lat, 2.1);
    course_free(&c);
}

static void test_track_selection(void)
{
    struct course c;
    char err[256];

    CHECK(load("two_tracks.gpx", &c, 0, err, sizeof err) == 0);
    CHECK_STR(c.name, "Long"); /* default: longest */
    CHECK(c.n_tp == 3);
    course_free(&c);

    CHECK(load("two_tracks.gpx", &c, 1, err, sizeof err) == 0);
    CHECK_STR(c.name, "Short");
    CHECK(c.n_tp == 2);
    course_free(&c);

    CHECK(load("two_tracks.gpx", &c, 2, err, sizeof err) == 0);
    CHECK_STR(c.name, "Long");
    course_free(&c);

    CHECK(load("two_tracks.gpx", &c, 3, err, sizeof err) != 0);
    CHECK(strstr(err, "track 3 does not exist (file has 2)") != NULL);
    course_free(&c);
}

/* T6: all <trk>s ordered and joined into one continuous course. */
static void test_stitch(void)
{
    static const double want_start[] = {
        0.00, 0.01, 0.02, 0.03, 0.04, 0.05, 0.06, 0.07, 0.08
    };
    static const double want_no_start[] = {
        0.06, 0.07, 0.08, 0.05, 0.04, 0.03, 0.02, 0.01, 0.00
    };
    struct course c;
    char err[256];

    /* Out-of-order file with "Start" at the end of the third (reversed) leg:
     * that leg is reversed and placed first, then Middle, then Tail. */
    CHECK(load_opts("multi_track.gpx", &c, 0, true, err, sizeof err) == 0);
    CHECK_STR(c.name, "Head");
    CHECK(c.n_tp == 9);
    for (size_t i = 0; i < c.n_tp && i < 9; i++)
        CHECK_NEAR(c.tp[i].lon, want_start[i]);
    CHECK(c.tp[0].has_ele && c.tp[0].ele == 0.0); /* reversed: 0 -> 2 */
    CHECK(c.tp[2].has_ele && c.tp[2].ele == 2.0);
    CHECK(c.n_wp == 3); /* waypoints are kept for snapping after the join */
    CHECK_STR(c.wp[0].name, "Start");
    CHECK_STR(c.wp[1].name, "Water");
    CHECK_STR(c.wp[2].name, "Camp");
    course_free(&c);

    /* Without an anchor, start with track 1 and greedily chain by nearest
     * endpoint; from Tail's 0.08 end, Middle is walked backwards (0.05 first),
     * then Head forward to 0.00. */
    CHECK(load_opts("multi_track_no_start.gpx", &c, 0, true,
                    err, sizeof err) == 0);
    CHECK_STR(c.name, "Tail");
    CHECK(c.n_tp == 9);
    for (size_t i = 0; i < c.n_tp && i < 9; i++)
        CHECK_NEAR(c.tp[i].lon, want_no_start[i]);
    CHECK(c.n_wp == 2);
    course_free(&c);

    /* A single track is unchanged by stitching. */
    CHECK(load_opts("gpx11_basic.gpx", &c, 0, true, err, sizeof err) == 0);
    CHECK_STR(c.name, "Basic Track");
    CHECK(c.n_tp == 3);
    CHECK_NEAR(c.tp[0].lat, 38.5816);
    course_free(&c);

    /* The parser ignores --track when stitching (the CLI rejects the pair). */
    CHECK(load_opts("multi_track.gpx", &c, 2, true, err, sizeof err) == 0);
    CHECK(c.n_tp == 9);
    course_free(&c);

    CHECK(load_opts("empty_track.gpx", &c, 0, true, err, sizeof err) != 0);
    CHECK(strstr(err, "no trackpoints found") != NULL);
    course_free(&c);
}

/* Case-insensitive anchor matching, and the exact-match convention. */
static void check_anchor_name(const char *wp_name, bool anchored)
{
    char buf[1024];
    struct gpx_options o = { .track = 0, .stitch = true, .quiet = true };
    struct course c;
    char err[256];

    snprintf(buf, sizeof buf,
             "<?xml version=\"1.0\"?>"
             "<gpx version=\"1.1\" xmlns=\"http://www.topografix.com/GPX/1/1\">"
             "<wpt lat=\"0\" lon=\"0\"><name>%s</name></wpt>"
             "<trk><name>A</name><trkseg>"
             "<trkpt lat=\"0\" lon=\"0.02\"/><trkpt lat=\"0\" lon=\"0\"/>"
             "</trkseg></trk>"
             "<trk><name>B</name><trkseg>"
             "<trkpt lat=\"0\" lon=\"0.04\"/><trkpt lat=\"0\" lon=\"0.03\"/>"
             "</trkseg></trk></gpx>", wp_name);
    course_init(&c);
    CHECK(read_gpx_mem(buf, strlen(buf), "mem", &c, &o, err, sizeof err) == 0);
    CHECK(c.n_tp == 4);
    if (c.n_tp == 4) {
        CHECK_NEAR(c.tp[0].lon, anchored ? 0.0 : 0.02);
        CHECK_NEAR(c.tp[1].lon, anchored ? 0.02 : 0.0);
        CHECK_NEAR(c.tp[2].lon, 0.03); /* B is reversed either way */
        CHECK_NEAR(c.tp[3].lon, 0.04);
    }
    course_free(&c);
}

/* T6: indices used for one-FIT-per-track output. */
static void test_track_indices(void)
{
    char path[256], err[256];
    long *idx = NULL;
    size_t n = 99;

    snprintf(path, sizeof path, DATA "multi_track.gpx");
    CHECK(gpx_track_indices(path, &idx, &n, err, sizeof err) == 0);
    CHECK(n == 3);
    if (n == 3)
        CHECK(idx[0] == 1 && idx[1] == 2 && idx[2] == 3);
    free(idx);

    idx = NULL;
    n = 99;
    snprintf(path, sizeof path, DATA "multi_track_no_start.gpx");
    CHECK(gpx_track_indices(path, &idx, &n, err, sizeof err) == 0);
    CHECK(n == 3);
    free(idx);

    idx = NULL;
    n = 99;
    snprintf(path, sizeof path, DATA "gpx11_basic.gpx");
    CHECK(gpx_track_indices(path, &idx, &n, err, sizeof err) == 0);
    CHECK(n == 1 && idx && idx[0] == 1);
    free(idx);

    /* Tracks without points are not courses. */
    idx = NULL;
    n = 99;
    snprintf(path, sizeof path, DATA "empty_track.gpx");
    CHECK(gpx_track_indices(path, &idx, &n, err, sizeof err) == 0);
    CHECK(n == 0 && idx == NULL);
    free(idx);

    idx = NULL;
    n = 99;
    snprintf(path, sizeof path, DATA "malformed.gpx");
    CHECK(gpx_track_indices(path, &idx, &n, err, sizeof err) != 0);
    free(idx);

    idx = NULL;
    n = 99;
    CHECK(gpx_track_indices(DATA "does_not_exist.gpx", &idx, &n,
                            err, sizeof err) != 0);
    CHECK(strstr(err, "cannot open") != NULL);
    free(idx);
}

static void test_errors(void)
{
    struct course c;
    char err[256];

    CHECK(load("does_not_exist.gpx", &c, 0, err, sizeof err) != 0);
    CHECK(strstr(err, "cannot open") != NULL);
    course_free(&c);

    expect_error("malformed.gpx", "line 5:");
    expect_error("empty_track.gpx", "no trackpoints");
    expect_error("not_gpx.gpx", "not a GPX file");
    expect_error("bad_lat.gpx", "line 5: invalid latitude 'abc'");
    expect_error("bad_lat_range.gpx", "invalid latitude '95.0'");
    expect_error("bad_lon_range.gpx", "invalid longitude '200.0'");
    expect_error("missing_lon.gpx", "missing its lon attribute");
    expect_error("bad_ele.gpx", "invalid elevation 'high'");
}

int main(void)
{
    test_names();
    test_gpx11();
    test_gpx10();
    test_prefixed();
    test_no_ele();
    test_unicode();
    test_segments();
    test_track_selection();
    test_stitch();
    check_anchor_name("START", true);
    check_anchor_name("  start  ", true);
    check_anchor_name("Start Here", false);
    test_track_indices();
    test_errors();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_gpx: all checks passed\n");
    return 0;
}
