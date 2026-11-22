/* Unit tests for the GPX reader (tasks 2.2-2.6). Run from the repo root. */
#include <math.h>
#include <stdio.h>
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
static int load(const char *file, struct course *c, long track, char *err,
                size_t errsz)
{
    char path[256];
    struct gpx_options o = { .track = track, .quiet = true };
    snprintf(path, sizeof path, DATA "%s", file);
    course_init(c);
    return read_gpx(path, c, &o, err, errsz);
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
    test_errors();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_gpx: all checks passed\n");
    return 0;
}
