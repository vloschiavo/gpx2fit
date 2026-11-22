/* test_geo.c: tests for src/geo.c (tasks 3.1 to 3.4). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/geo.h"

static int failures;
static int checks;

#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                              \
    do {                                                                   \
        double a_ = (a), b_ = (b), t_ = (tol);                             \
        checks++;                                                          \
        if (!(fabs(a_ - b_) <= t_)) {                                      \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s = %.9g, expected %.9g +/- %g\n", \
                    __FILE__, __LINE__, #a, a_, b_, t_);                   \
        }                                                                  \
    } while (0)

/* Relative tolerance check. */
#define CHECK_REL(a, b, rel) CHECK_NEAR((a), (b), fabs(b) * (rel))

/* One degree of arc on the mean-radius sphere, meters. */
#define DEG_M (EARTH_RADIUS_M * 3.14159265358979323846 / 180.0)

/* Build a course with n trackpoints allocated directly (no course.c). */
static void make_course(struct course *c, size_t n)
{
    memset(c, 0, sizeof *c);
    c->tp = calloc(n ? n : 1, sizeof *c->tp);
    c->n_tp = c->cap_tp = n;
    if (!c->tp) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
}

/* ---- 3.1 ---------------------------------------------------------------- */

static void test_haversine(void)
{
    /* One degree of latitude is about 111.19 km. */
    CHECK_NEAR(haversine_m(0, 0, 1, 0), 111195.08, 1.0);
    CHECK_NEAR(haversine_m(45, 10, 46, 10), 111195.08, 1.0);
    /* One degree of longitude on the equator is the same. */
    CHECK_NEAR(haversine_m(0, 0, 0, 1), 111195.08, 1.0);
    /* At 60 degrees, a degree of longitude is half as long. */
    CHECK_NEAR(haversine_m(60, 0, 60, 1), 111195.08 / 2.0, 100.0);
    /* Identical points, symmetry. */
    CHECK(haversine_m(38.5, -121.5, 38.5, -121.5) == 0.0);
    CHECK_NEAR(haversine_m(10, 20, 30, 40), haversine_m(30, 40, 10, 20), 1e-6);
    /* Half the Earth's circumference, and the antipodal clamp case. */
    CHECK_NEAR(haversine_m(0, 0, 0, 180), 3.14159265358979323846 * EARTH_RADIUS_M, 1.0);
    CHECK_NEAR(haversine_m(90, 0, -90, 0), 3.14159265358979323846 * EARTH_RADIUS_M, 1.0);
    CHECK(!isnan(haversine_m(12.3, 45.6, -12.3, -134.4)));
    /* Antimeridian: 179.5 to -179.5 is one degree, not 359. */
    CHECK_NEAR(haversine_m(0, 179.5, 0, -179.5), DEG_M, 1.0);
}

static void test_cumulative_distance(void)
{
    struct course c;

    /* Empty and single-point courses are safe. */
    make_course(&c, 0);
    course_compute_distances(&c);
    free(c.tp);
    make_course(&c, 1);
    c.tp[0].dist = 99.0;
    course_compute_distances(&c);
    CHECK(c.tp[0].dist == 0.0);
    free(c.tp);

    /* Along a meridian, with a repeated point in the middle. */
    make_course(&c, 5);
    double lats[5] = {0.0, 0.5, 0.5, 1.0, 2.0};
    for (size_t i = 0; i < 5; i++) {
        c.tp[i].lat = lats[i];
        c.tp[i].lon = 10.0;
        c.tp[i].dist = -1.0; /* must be overwritten */
    }
    course_compute_distances(&c);
    CHECK(c.tp[0].dist == 0.0);
    CHECK_NEAR(c.tp[1].dist, 0.5 * DEG_M, 0.5);
    CHECK(c.tp[2].dist == c.tp[1].dist); /* zero-length segment adds 0 */
    CHECK_NEAR(c.tp[3].dist, 1.0 * DEG_M, 1.0);
    CHECK_NEAR(c.tp[4].dist, 2.0 * DEG_M, 2.0);
    for (size_t i = 1; i < 5; i++)
        CHECK(c.tp[i].dist >= c.tp[i - 1].dist);
    free(c.tp);
}

/* ---- 3.2 ---------------------------------------------------------------- */

static void test_projection_roundtrip(void)
{
    struct local_proj p;
    proj_init(&p, 38.5, -121.5);

    double x, y, lat, lon;
    proj_forward(&p, 38.5, -121.5, &x, &y);
    CHECK_NEAR(x, 0.0, 1e-9);
    CHECK_NEAR(y, 0.0, 1e-9);

    /* Round trip on a grid within about 0.5 degrees of the reference. */
    for (int i = -5; i <= 5; i++) {
        for (int j = -5; j <= 5; j++) {
            double la = 38.5 + i * 0.1, lo = -121.5 + j * 0.1;
            proj_forward(&p, la, lo, &x, &y);
            proj_inverse(&p, x, y, &lat, &lon);
            CHECK_NEAR(lat, la, 1e-9);
            CHECK_NEAR(lon, lo, 1e-9);
        }
    }

    /* Antimeridian round trip. */
    proj_init(&p, 10.0, 179.9);
    proj_forward(&p, 10.0, -179.9, &x, &y);
    CHECK(x > 0.0 && x < 30000.0); /* about 0.2 degrees east, not 360 away */
    proj_inverse(&p, x, y, &lat, &lon);
    CHECK_NEAR(lon, -179.9, 1e-9);
}

/* Planar distance between two points under a projection. */
static double proj_dist(const struct local_proj *p, double la1, double lo1,
                        double la2, double lo2)
{
    double x1, y1, x2, y2;
    proj_forward(p, la1, lo1, &x1, &y1);
    proj_forward(p, la2, lo2, &x2, &y2);
    return hypot(x2 - x1, y2 - y1);
}

static void test_projection_vs_haversine(void)
{
    struct local_proj p;
    double worst = 0.0;

    /* Tracks of 100 km at several latitudes and bearings, projection
     * centered at the track midpoint. Spans about +/-50 km. */
    const double lats[] = {0.0, 38.5, 55.0};
    for (size_t k = 0; k < sizeof lats / sizeof *lats; k++) {
        double lat0 = lats[k], lon0 = 10.0;
        proj_init(&p, lat0, lon0);
        for (int deg = 0; deg < 360; deg += 15) {
            double b = deg * 3.14159265358979323846 / 180.0;
            /* Endpoints 50 km either side of the middle (100 km chord). */
            double dla = 50000.0 * cos(b) / DEG_M;
            double dlo = 50000.0 * sin(b) / (DEG_M * cos(lat0 * 3.14159265358979323846 / 180.0));
            double d_h = haversine_m(lat0 - dla, lon0 - dlo, lat0 + dla, lon0 + dlo);
            double d_p = proj_dist(&p, lat0 - dla, lon0 - dlo, lat0 + dla, lon0 + dlo);
            double rel = fabs(d_p - d_h) / d_h;
            if (rel > worst)
                worst = rel;
            CHECK(rel < 0.001);
        }
    }
    printf("  projection vs haversine, 100 km chords through the center: worst error %.4f%%\n",
           worst * 100.0);

    /* Short distances (what snapping uses): well under 0.01%. */
    proj_init(&p, 38.5, -121.5);
    double d_h = haversine_m(38.5, -121.5, 38.502, -121.497);
    double d_p = proj_dist(&p, 38.5, -121.5, 38.502, -121.497);
    CHECK_REL(d_p, d_h, 1e-4);
}

/* ---- 3.3 ---------------------------------------------------------------- */

static void test_project_to_segment(void)
{
    struct seg_proj r;

    /* East-west segment on the equator-ish; point 100 m north of its middle. */
    double off = 100.0 / DEG_M;
    r = project_to_segment(off, 0.0005, 0.0, 0.0, 0.0, 0.001);
    CHECK_NEAR(r.t, 0.5, 1e-6);
    CHECK_NEAR(r.dist_m, 100.0, 0.01);
    CHECK_NEAR(r.lat, 0.0, 1e-9);
    CHECK_NEAR(r.lon, 0.0005, 1e-9);

    /* Perpendicular at 25% of a slanted segment at 38.5N. */
    {
        double alat = 38.5, alon = -121.5, blat = 38.5, blon = -121.49;
        double plon = alon + 0.25 * (blon - alon);
        r = project_to_segment(alat + off, plon, alat, alon, blat, blon);
        CHECK_NEAR(r.t, 0.25, 1e-6);
        CHECK_NEAR(r.dist_m, 100.0, 0.01);
    }

    /* Before the start: clamps to A, distance is to A. */
    r = project_to_segment(0.0, -0.001, 0.0, 0.0, 0.0, 0.001);
    CHECK(r.t == 0.0);
    CHECK_NEAR(r.lat, 0.0, 1e-12);
    CHECK_NEAR(r.lon, 0.0, 1e-12);
    CHECK_NEAR(r.dist_m, 0.001 * DEG_M, 0.01);

    /* After the end: clamps to B. */
    r = project_to_segment(0.0, 0.002, 0.0, 0.0, 0.0, 0.001);
    CHECK(r.t == 1.0);
    CHECK_NEAR(r.lon, 0.001, 1e-12);
    CHECK_NEAR(r.dist_m, 0.001 * DEG_M, 0.01);

    /* Point on the segment: zero offset. */
    r = project_to_segment(10.0, 20.0005, 10.0, 20.0, 10.0, 20.001);
    CHECK_NEAR(r.t, 0.5, 1e-6);
    CHECK_NEAR(r.dist_m, 0.0, 1e-6);

    /* Degenerate segment: A == B. */
    r = project_to_segment(0.001, 0.0, 0.0, 0.0, 0.0, 0.0);
    CHECK(r.t == 0.0);
    CHECK_NEAR(r.lat, 0.0, 1e-12);
    CHECK_NEAR(r.lon, 0.0, 1e-12);
    CHECK_NEAR(r.dist_m, 0.001 * DEG_M, 0.01);
    CHECK(!isnan(r.dist_m));

    /* Degenerate segment with the point exactly on it. */
    r = project_to_segment(5.0, 5.0, 5.0, 5.0, 5.0, 5.0);
    CHECK(r.t == 0.0);
    CHECK(r.dist_m == 0.0);

    /* Segment across the antimeridian. */
    r = project_to_segment(0.0, 180.0, 0.0, 179.999, 0.0, -179.999);
    CHECK_NEAR(r.t, 0.5, 1e-6);
    CHECK_NEAR(r.dist_m, 0.0, 1e-6);
}

/* ---- 3.4 ---------------------------------------------------------------- */

static void set_profile(struct course *c, const double *e, const int *has,
                        size_t n)
{
    for (size_t i = 0; i < n; i++) {
        c->tp[i].ele = e[i];
        c->tp[i].has_ele = has ? has[i] != 0 : true;
    }
}

static void test_elevation(void)
{
    struct course c;
    struct ele_stats s;

    /* Known profile, threshold 1 m: +5, -2, (0.5 ignored), +7 -> 12 up, 2 down. */
    {
        double e[] = {0, 5, 3, 3.5, 10};
        make_course(&c, 5);
        set_profile(&c, e, NULL, 5);
        course_elevation_stats(&c, 1.0, &s);
        CHECK(s.valid);
        CHECK_NEAR(s.ascent, 12.0, 1e-9);
        CHECK_NEAR(s.descent, 2.0, 1e-9);
        CHECK_NEAR(s.min_ele, 0.0, 1e-9);
        CHECK_NEAR(s.max_ele, 10.0, 1e-9);

        /* Threshold 0 counts every change: 0->5->3->3.5->10 */
        course_elevation_stats(&c, 0.0, &s);
        CHECK_NEAR(s.ascent, 5.0 + 0.5 + 6.5, 1e-9);
        CHECK_NEAR(s.descent, 2.0, 1e-9);
        free(c.tp);
    }

    /* Noise below the threshold is ignored. */
    {
        double e[] = {100, 100.4, 99.8, 100.3, 99.9, 100.2};
        make_course(&c, 6);
        set_profile(&c, e, NULL, 6);
        course_elevation_stats(&c, 1.0, &s);
        CHECK_NEAR(s.ascent, 0.0, 1e-9);
        CHECK_NEAR(s.descent, 0.0, 1e-9);
        CHECK_NEAR(s.min_ele, 99.8, 1e-9);
        CHECK_NEAR(s.max_ele, 100.4, 1e-9);
        free(c.tp);
    }

    /* A slow steady climb of 10 m in 0.4 m steps is not lost. */
    {
        make_course(&c, 26);
        for (size_t i = 0; i < 26; i++) {
            c.tp[i].ele = 0.4 * (double)i;
            c.tp[i].has_ele = true;
        }
        course_elevation_stats(&c, 1.0, &s);
        CHECK(s.ascent > 10.0 - 1.0 - 1e-9 && s.ascent <= 10.0 + 1e-9);
        CHECK_NEAR(s.descent, 0.0, 1e-9);
        free(c.tp);
    }

    /* Points without elevation are skipped, not treated as 0. */
    {
        double e[] = {10, 0, 15, 0, 12};
        int has[] = {1, 0, 1, 0, 1};
        make_course(&c, 5);
        set_profile(&c, e, has, 5);
        course_elevation_stats(&c, 1.0, &s);
        CHECK(s.valid);
        CHECK_NEAR(s.ascent, 5.0, 1e-9);
        CHECK_NEAR(s.descent, 3.0, 1e-9);
        CHECK_NEAR(s.min_ele, 10.0, 1e-9);
        CHECK_NEAR(s.max_ele, 15.0, 1e-9);
        free(c.tp);
    }

    /* No elevation at all, and an empty course. */
    {
        double e[] = {1, 2, 3};
        int has[] = {0, 0, 0};
        make_course(&c, 3);
        set_profile(&c, e, has, 3);
        course_elevation_stats(&c, 1.0, &s);
        CHECK(!s.valid);
        CHECK(s.ascent == 0.0 && s.descent == 0.0);
        free(c.tp);

        make_course(&c, 0);
        course_elevation_stats(&c, 1.0, &s);
        CHECK(!s.valid);
        free(c.tp);
    }

    /* Single point with elevation; negative threshold treated as 0. */
    {
        double e[] = {42, 43};
        make_course(&c, 2);
        set_profile(&c, e, NULL, 2);
        course_elevation_stats(&c, -5.0, &s);
        CHECK_NEAR(s.ascent, 1.0, 1e-9);
        free(c.tp);
    }
}

int main(void)
{
    test_haversine();
    test_cumulative_distance();
    test_projection_roundtrip();
    test_projection_vs_haversine();
    test_project_to_segment();
    test_elevation();

    if (failures) {
        fprintf(stderr, "test_geo: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_geo: all %d checks passed\n", checks);
    return 0;
}
