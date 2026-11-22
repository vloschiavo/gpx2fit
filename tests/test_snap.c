/* test_snap.c: tests for src/snap.c (tasks 4.1 to 4.7). */
#define _POSIX_C_SOURCE 200809L /* open_memstream */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/snap.h"

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

#define PI_ 3.14159265358979323846
#define DEG_M (EARTH_RADIUS_M * PI_ / 180.0)

/* ---- fixture helpers: build tracks in local meters --------------------- */

static double ref_lat = 38.5, ref_lon = -121.5;

static void xy2ll(double x, double y, double *lat, double *lon)
{
    double l;
    *lat = ref_lat + y / DEG_M;
    l = ref_lon + x / (DEG_M * cos(ref_lat * PI_ / 180.0));
    while (l > 180.0)
        l -= 360.0;
    while (l < -180.0)
        l += 360.0;
    *lon = l;
}

static void add_tp(struct course *c, double x, double y)
{
    struct trackpoint tp;
    memset(&tp, 0, sizeof tp);
    xy2ll(x, y, &tp.lat, &tp.lon);
    if (course_add_trackpoint(c, &tp) != 0) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
}

static void add_wp(struct course *c, const char *name, double x, double y)
{
    double lat, lon;
    xy2ll(x, y, &lat, &lon);
    if (course_add_waypoint(c, name, lat, lon) != 0) {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }
}

/* Track from an array of {x, y} pairs; computes cumulative distances. */
static void make_track(struct course *c, const double (*pts)[2], size_t n)
{
    course_init(c);
    for (size_t i = 0; i < n; i++)
        add_tp(c, pts[i][0], pts[i][1]);
    course_compute_distances(c);
}

static int run(struct course *c, enum snap_mode m, double thr, bool strict,
               struct snap_report *rep)
{
    struct snap_options o = { m, thr, true, strict };
    return snap_waypoints(c, &o, rep);
}

static const struct course_pt *find_cp(const struct course *c, const char *name)
{
    for (size_t i = 0; i < c->n_cp; i++)
        if (strcmp(c->cp[i].name, name) == 0)
            return &c->cp[i];
    return NULL;
}

/* Fixtures (x, y in meters). */
static const double STRAIGHT[][2] = { {0, 0}, {1000, 0}, {2000, 0}, {3000, 0} };
static const double LOOP[][2] = { {0, 0}, {1000, 0}, {1000, 1000}, {0, 1000},
                                  {0, 0} };
/* Out to x = 2000 and straight back over the same ground. */
static const double OUTBACK[][2] = { {0, 0}, {1000, 0}, {2000, 0}, {1000, 0},
                                     {0, 0} };
/* Stem along y = 0 walked twice, with a loop at the far end. */
static const double LOLLI[][2] = { {0, 0}, {1000, 0}, {2000, 0}, {2000, 1000},
                                   {1000, 1000}, {1000, 0}, {0, 0} };

/* ---- 4.1 prefilter vs brute force -------------------------------------- */

static unsigned long long rng_state = 88172645463325252ULL;

static double rnd(void) /* uniform in [0, 1) */
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (double)(rng_state >> 11) / 9007199254740992.0;
}

static void check_prefilter_against_brute(const struct course *c,
                                          double span_m, double *share_out)
{
    struct snap_index ix;
    const double thrs[] = { 0.0, 50.0, 250.0, 1500.0 };
    double kept = 0.0, total = 0.0;

    CHECK(snap_index_init(&ix, c) == 0);
    for (size_t ti = 0; ti < sizeof thrs / sizeof *thrs; ti++) {
        double thr = thrs[ti];
        for (int q = 0; q < 150; q++) {
            double lat, lon;
            size_t *segs, ns, nb = 0, nf = 0;
            size_t *brute = malloc(ix.n_seg * sizeof *brute);
            size_t *filt = malloc(ix.n_seg * sizeof *filt);

            xy2ll((rnd() - 0.5) * span_m, (rnd() - 0.5) * span_m, &lat, &lon);

            /* Reference: exact projection against every segment. */
            for (size_t i = 0; i < ix.n_seg; i++) {
                struct seg_proj p = project_to_segment(lat, lon,
                    c->tp[i].lat, c->tp[i].lon,
                    c->tp[i + 1].lat, c->tp[i + 1].lon);
                if (p.dist_m <= thr)
                    brute[nb++] = i;
            }
            /* Prefilter, then the same exact test on the survivors. */
            CHECK(snap_prefilter(&ix, lat, lon, thr, &segs, &ns) == 0);
            for (size_t k = 0; k < ns; k++) {
                size_t i = segs[k];
                struct seg_proj p = project_to_segment(lat, lon,
                    c->tp[i].lat, c->tp[i].lon,
                    c->tp[i + 1].lat, c->tp[i + 1].lon);
                if (p.dist_m <= thr)
                    filt[nf++] = i;
            }
            CHECK(nf == nb);
            for (size_t k = 0; k < nb && k < nf; k++)
                CHECK(brute[k] == filt[k]);
            if (thr == 250.0) {
                kept += (double)ns;
                total += (double)ix.n_seg;
            }
            free(segs);
            free(brute);
            free(filt);
        }
    }
    snap_index_free(&ix);
    *share_out = total > 0.0 ? kept / total : 0.0;
}

static void walk(struct course *c, size_t n, double lat0_, double lon0_)
{
    double x = 0.0, y = 0.0;
    ref_lat = lat0_;
    ref_lon = lon0_;
    course_init(c);
    for (size_t i = 0; i < n; i++) {
        add_tp(c, x, y);
        x += (rnd() - 0.5) * 300.0;
        y += (rnd() - 0.5) * 300.0;
    }
    course_compute_distances(c);
}

static void test_prefilter(void)
{
    struct course c;
    double share;

    /* Random walk of 3000 points, queries all over and beyond it. */
    walk(&c, 3000, 38.5, -121.5);
    check_prefilter_against_brute(&c, 12000.0, &share);
    printf("  prefilter keeps %.1f%% of segments at 250 m\n", share * 100.0);
    CHECK(share < 0.25); /* it really prunes */
    course_free(&c);

    /* Same, straddling the antimeridian. */
    walk(&c, 3000, 10.0, 179.97);
    check_prefilter_against_brute(&c, 12000.0, &share);
    course_free(&c);
    ref_lat = 38.5;
    ref_lon = -121.5;

    /* Edge cases: no segments at all. */
    {
        struct snap_index ix;
        size_t *segs, n;
        course_init(&c);
        CHECK(snap_index_init(&ix, &c) == 0);
        CHECK(snap_prefilter(&ix, 0, 0, 100, &segs, &n) == 0);
        CHECK(n == 0 && segs == NULL);
        snap_index_free(&ix);
        add_tp(&c, 0, 0);
        CHECK(snap_index_init(&ix, &c) == 0);
        CHECK(ix.n_seg == 0);
        snap_index_free(&ix);
        course_free(&c);
    }
}

/* ---- 4.2 candidates ---------------------------------------------------- */

static int cands_for(const struct course *c, double x, double y, double thr,
                     struct snap_cands *cs)
{
    struct snap_index ix;
    double lat, lon;
    int rc;

    xy2ll(x, y, &lat, &lon);
    if (snap_index_init(&ix, c) != 0)
        return -1;
    rc = snap_candidates(&ix, lat, lon, thr, cs);
    snap_index_free(&ix);
    return rc;
}

static void test_candidates(void)
{
    struct course c;
    struct snap_cands cs;

    /* Straight track: interior hit, and both ends. */
    make_track(&c, STRAIGHT, 4);
    CHECK(cands_for(&c, 500, 30, 250, &cs) == 0);
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 500.0, 0.5);
    CHECK_NEAR(cs.v[0].offset, 30.0, 0.05);
    CHECK_NEAR(cs.v[0].t, 0.5, 1e-3);
    snap_cands_free(&cs);

    CHECK(cands_for(&c, -50, 0, 250, &cs) == 0); /* before the start */
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 0.0, 1e-6);
    CHECK_NEAR(cs.v[0].offset, 50.0, 0.05);
    snap_cands_free(&cs);

    CHECK(cands_for(&c, 3200, 0, 250, &cs) == 0); /* after the end */
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 3000.0, 0.5);
    snap_cands_free(&cs);

    /* Near a vertex of a straight line: the neighbouring segment is within
     * the threshold too, but clamps at the vertex and must not add a
     * second candidate. */
    CHECK(cands_for(&c, 900, 30, 250, &cs) == 0);
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 900.0, 0.5);
    snap_cands_free(&cs);
    CHECK(cands_for(&c, 1100, 30, 250, &cs) == 0);
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 1100.0, 0.5);
    snap_cands_free(&cs);

    CHECK(cands_for(&c, 1500, 400, 250, &cs) == 0); /* too far */
    CHECK(cs.n == 0 && cs.v == NULL);
    snap_cands_free(&cs);
    course_free(&c);

    /* Out-and-back: two passes on the shared section, one at the turn. */
    make_track(&c, OUTBACK, 5);
    CHECK(cands_for(&c, 500, 0, 250, &cs) == 0);
    CHECK(cs.n == 2);
    if (cs.n == 2) {
        CHECK_NEAR(cs.v[0].dist, 500.0, 0.5);
        CHECK_NEAR(cs.v[1].dist, 3500.0, 0.5);
        CHECK_NEAR(cs.v[0].offset, 0.0, 1e-3);
        CHECK_NEAR(cs.v[1].offset, 0.0, 1e-3);
    }
    snap_cands_free(&cs);
    CHECK(cands_for(&c, 2000, 0, 250, &cs) == 0); /* the turnaround vertex */
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 2000.0, 0.5);
    snap_cands_free(&cs);
    course_free(&c);

    /* Lollipop: stem passes twice, the far loop once. */
    make_track(&c, LOLLI, 7);
    CHECK(cands_for(&c, 500, 0, 250, &cs) == 0);
    CHECK(cs.n == 2);
    snap_cands_free(&cs);
    CHECK(cands_for(&c, 2000, 500, 250, &cs) == 0);
    CHECK(cs.n == 1);
    CHECK_NEAR(cs.v[0].dist, 2500.0, 0.5);
    snap_cands_free(&cs);
    /* The junction vertex is visited twice (1000 m and 5000 m). */
    CHECK(cands_for(&c, 1000, -50, 250, &cs) == 0);
    CHECK(cs.n == 2);
    if (cs.n == 2) {
        CHECK_NEAR(cs.v[0].dist, 1000.0, 0.5);
        CHECK_NEAR(cs.v[1].dist, 5000.0, 0.5);
    }
    snap_cands_free(&cs);
    course_free(&c);

    /* Loop: a waypoint beside the start/finish line sees both ends. */
    make_track(&c, LOOP, 5);
    CHECK(cands_for(&c, 10, 5, 250, &cs) == 0);
    CHECK(cs.n == 2);
    if (cs.n == 2) {
        CHECK_NEAR(cs.v[0].dist, 10.0, 0.5);
        CHECK_NEAR(cs.v[1].dist, 3995.0, 0.5);
    }
    snap_cands_free(&cs);
    course_free(&c);

    /* Repeated trackpoints do not add or hide candidates. */
    {
        const double dup[][2] = { {0, 0}, {1000, 0}, {1000, 0}, {1000, 0},
                                  {2000, 0} };
        make_track(&c, dup, 5);
        CHECK(cands_for(&c, 1000, 20, 250, &cs) == 0); /* at the vertex */
        CHECK(cs.n == 1);
        CHECK_NEAR(cs.v[0].dist, 1000.0, 0.5);
        snap_cands_free(&cs);
        CHECK(cands_for(&c, 1500, 20, 250, &cs) == 0);
        CHECK(cs.n == 1);
        snap_cands_free(&cs);
        course_free(&c);
    }

    /* GPS jitter on a single pass is one candidate, not many. */
    {
        struct course z;
        course_init(&z);
        for (int i = 0; i <= 20; i++)
            add_tp(&z, i * 10.0, (i % 2) ? -2.0 : 2.0);
        course_compute_distances(&z);
        CHECK(cands_for(&z, 100, 0.5, 250, &cs) == 0);
        CHECK(cs.n == 1);
        snap_cands_free(&cs);
        course_free(&z);
    }
}

/* ---- 4.3 to 4.5 modes -------------------------------------------------- */

static void test_modes(void)
{
    struct course c;
    struct snap_report rep;
    enum snap_mode m;

    CHECK(snap_mode_parse("nearest", &m) == 0 && m == SNAP_NEAREST);
    CHECK(snap_mode_parse("ordered", &m) == 0 && m == SNAP_ORDERED);
    CHECK(snap_mode_parse("first", &m) == 0 && m == SNAP_FIRST);
    CHECK(snap_mode_parse("last", &m) == 0 && m == SNAP_LAST);
    CHECK(snap_mode_parse("Nearest", &m) != 0);
    CHECK(snap_mode_parse("", &m) != 0);
    CHECK(strcmp(snap_mode_name(SNAP_ORDERED), "ordered") == 0);

    /* 4.3: straight line, nearest. */
    make_track(&c, STRAIGHT, 4);
    add_wp(&c, "A", 500, 30);
    add_wp(&c, "B", 2500, -100);
    add_wp(&c, "Before", -50, 0);
    add_wp(&c, "After", 3200, 0);
    CHECK(run(&c, SNAP_NEAREST, 250, false, &rep) == SNAP_OK);
    CHECK(rep.n_snapped == 4 && rep.n_skipped == 0);
    CHECK(c.n_cp == 4);
    CHECK_NEAR(find_cp(&c, "A")->dist, 500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "A")->offset, 30.0, 0.05);
    CHECK_NEAR(find_cp(&c, "B")->dist, 2500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Before")->dist, 0.0, 1e-6);
    CHECK_NEAR(find_cp(&c, "After")->dist, 3000.0, 0.5);
    /* The snapped position is on the track (y = 0). */
    CHECK_NEAR(find_cp(&c, "A")->lat, ref_lat, 1e-9);
    CHECK(c.cp[0].type == 0 && c.cp[0].t == 0);
    course_free(&c);

    /* 4.3: loop. Start/finish waypoint sits closer to the start. */
    make_track(&c, LOOP, 5);
    add_wp(&c, "SF", 10, 5);
    add_wp(&c, "Side", 1000, 500);
    CHECK(run(&c, SNAP_NEAREST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "SF")->dist, 10.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Side")->dist, 1500.0, 0.5);
    CHECK(run(&c, SNAP_FIRST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "SF")->dist, 10.0, 0.5);
    CHECK(run(&c, SNAP_LAST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "SF")->dist, 3995.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Side")->dist, 1500.0, 0.5); /* one pass only */
    course_free(&c);

    /* 4.4 and 4.5: out-and-back, waypoints listed outbound then return. */
    make_track(&c, OUTBACK, 5);
    add_wp(&c, "Out", 500, 0);
    add_wp(&c, "Turn", 2000, 0);
    add_wp(&c, "Back", 500, 0);
    CHECK(run(&c, SNAP_NEAREST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "Back")->dist, 500.0, 0.5); /* tie -> earlier */
    CHECK(run(&c, SNAP_FIRST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "Back")->dist, 500.0, 0.5);
    CHECK(run(&c, SNAP_LAST, 250, false, &rep) == SNAP_OK);
    CHECK_NEAR(find_cp(&c, "Out")->dist, 3500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Turn")->dist, 2000.0, 0.5);
    CHECK(run(&c, SNAP_ORDERED, 250, false, &rep) == SNAP_OK);
    CHECK(rep.n_unordered == 0 && rep.n_snapped == 3);
    CHECK_NEAR(find_cp(&c, "Out")->dist, 500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Turn")->dist, 2000.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Back")->dist, 3500.0, 0.5); /* the return pass */
    CHECK(c.cp[0].dist <= c.cp[1].dist && c.cp[1].dist <= c.cp[2].dist);
    course_free(&c);

    /* 4.4: GPX order contradicts the track: the best chain wins, the other
     * waypoint falls back to its nearest pass and is reported. */
    make_track(&c, STRAIGHT, 4);
    add_wp(&c, "Z", 2500, 40);
    add_wp(&c, "A", 500, 10);
    CHECK(run(&c, SNAP_ORDERED, 250, false, &rep) == SNAP_OK);
    CHECK(rep.n_snapped == 2 && rep.n_unordered == 1);
    CHECK_NEAR(find_cp(&c, "A")->dist, 500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Z")->dist, 2500.0, 0.5);
    CHECK(strcmp(c.cp[0].name, "A") == 0 && strcmp(c.cp[1].name, "Z") == 0);
    course_free(&c);

    /* 4.4: a skipped waypoint does not take part in the ordering. */
    make_track(&c, OUTBACK, 5);
    add_wp(&c, "Out", 500, 0);
    add_wp(&c, "Far", 500, 900);
    add_wp(&c, "Back", 500, 0);
    CHECK(run(&c, SNAP_ORDERED, 250, false, &rep) == SNAP_OK);
    CHECK(rep.n_skipped == 1 && rep.n_snapped == 2 && rep.n_unordered == 0);
    CHECK_NEAR(find_cp(&c, "Out")->dist, 500.0, 0.5);
    CHECK_NEAR(find_cp(&c, "Back")->dist, 500.0, 0.5); /* same spot: allowed */
    course_free(&c);
}

/* ---- 4.6 sorting, strict, summary -------------------------------------- */

static void test_sort_strict_summary(void)
{
    struct course c;
    struct snap_report rep;

    /* Sorted by distance regardless of GPX order. */
    make_track(&c, STRAIGHT, 4);
    add_wp(&c, "C", 2500, 5);
    add_wp(&c, "A", 500, 30);
    add_wp(&c, "B", 1500, 5);
    add_wp(&c, "Far", 1500, 400); /* the 400 m waypoint */
    CHECK(run(&c, SNAP_NEAREST, 250, false, &rep) == SNAP_OK);
    CHECK(rep.n_skipped == 1 && rep.n_snapped == 3 && c.n_cp == 3);
    CHECK(strcmp(c.cp[0].name, "A") == 0);
    CHECK(strcmp(c.cp[1].name, "B") == 0);
    CHECK(strcmp(c.cp[2].name, "C") == 0);
    CHECK(find_cp(&c, "Far") == NULL);
    CHECK_NEAR(snap_nearest_offset(&c, c.wp[3].lat, c.wp[3].lon), 400.0, 0.5);

    /* --strict: nonzero if anything was skipped, course points still made. */
    CHECK(run(&c, SNAP_NEAREST, 250, true, &rep) == SNAP_STRICT);
    CHECK(c.n_cp == 3);
    CHECK(run(&c, SNAP_NEAREST, 500, true, &rep) == SNAP_OK); /* now in range */
    CHECK(rep.n_skipped == 0 && c.n_cp == 4);
    /* Running again replaces the previous course points. */
    CHECK(run(&c, SNAP_NEAREST, 250, false, &rep) == SNAP_OK);
    CHECK(c.n_cp == 3);

    /* Summary text: one line per course point, then the tally. */
    {
        char *buf = NULL;
        size_t len = 0;
        FILE *f = open_memstream(&buf, &len);
        CHECK(f != NULL);
        if (f) {
            snap_print_summary(f, &c, &rep);
            fclose(f);
            CHECK(strstr(buf, "A ") != NULL);
            CHECK(strstr(buf, "0.50 km") != NULL);
            CHECK(strstr(buf, "offset  30.0 m") != NULL);
            CHECK(strstr(buf, "Snapped 3 of 4 waypoints (1 skipped)") != NULL);
            free(buf);
        }
    }
    course_free(&c);

    /* Empty waypoint list and an empty track are safe. */
    course_init(&c);
    CHECK(run(&c, SNAP_NEAREST, 250, true, &rep) == SNAP_OK);
    CHECK(c.n_cp == 0);
    add_wp(&c, "Lost", 0, 0);
    CHECK(run(&c, SNAP_NEAREST, 250, true, &rep) == SNAP_STRICT);
    CHECK(rep.n_skipped == 1);
    course_free(&c);
}

/* ---- 4.7 remaining fixtures -------------------------------------------- */

static void test_duplicates(void)
{
    struct course c;
    struct snap_report rep;

    make_track(&c, STRAIGHT, 4);
    add_wp(&c, "First", 500, 20);
    add_wp(&c, "Second", 500, 20); /* same spot, different name */
    add_wp(&c, "Third", 500, 20);
    for (int m = SNAP_NEAREST; m <= SNAP_LAST; m++) {
        CHECK(run(&c, (enum snap_mode)m, 250, false, &rep) == SNAP_OK);
        CHECK(c.n_cp == 3);
        CHECK(c.cp[0].dist == c.cp[1].dist && c.cp[1].dist == c.cp[2].dist);
        /* Equal distances keep GPX order (stable sort). */
        CHECK(strcmp(c.cp[0].name, "First") == 0);
        CHECK(strcmp(c.cp[1].name, "Second") == 0);
        CHECK(strcmp(c.cp[2].name, "Third") == 0);
    }
    course_free(&c);
}

int main(void)
{
    test_prefilter();
    test_candidates();
    test_modes();
    test_sort_strict_summary();
    test_duplicates();

    if (failures) {
        fprintf(stderr, "test_snap: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_snap: all %d checks passed\n", checks);
    return 0;
}
