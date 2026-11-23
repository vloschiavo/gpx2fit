/* test_decimate.c: tests for course_decimate() (task 8.2). */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "course.h"
#include "decimate.h"
#include "geo.h"

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

/* A dense, wiggly east-going track: n points about 3 m apart with a sine
 * wobble, so Douglas-Peucker has real shape to keep. */
static void make_track(struct course *c, size_t n)
{
    course_init(c);
    for (size_t i = 0; i < n; i++) {
        struct trackpoint tp = {0};
        tp.lat = 38.5 + 0.002 * sin((double)i / 150.0) + 0.0005 * sin((double)i / 17.0);
        tp.lon = -121.5 + (double)i * 0.00004;
        tp.ele = 30.0 + (double)(i % 40);
        tp.has_ele = true;
        tp.t = (uint32_t)(1000 + i);
        course_add_trackpoint(c, &tp);
    }
    course_compute_distances(c);
}

static void add_cp(struct course *c, const char *name, double dist)
{
    struct course_pt cp = {0};
    struct course_pt *p = realloc(c->cp, (c->n_cp + 1) * sizeof *c->cp);

    c->cp = p;
    cp.name = strdup(name);
    cp.dist = dist;
    c->cp[c->n_cp++] = cp;
    c->cap_cp = c->n_cp;
}

/* Index of the trackpoint of the saved full-resolution distances nearest d. */
static size_t nearest(const double *dist, size_t n, double d)
{
    size_t best = 0;
    for (size_t i = 1; i < n; i++)
        if (fabs(dist[i] - d) < fabs(dist[best] - d))
            best = i;
    return best;
}

static bool has_dist(const struct course *c, double d)
{
    for (size_t i = 0; i < c->n_tp; i++)
        if (c->tp[i].dist == d)
            return true;
    return false;
}

static void test_dense_reduction(void)
{
    const size_t N = 5000, MAXP = 200;
    struct course c;
    struct decimate_report r;
    double *full = malloc(N * sizeof *full);
    uint32_t *full_t = malloc(N * sizeof *full_t);
    double first_lat, first_lon, last_lat, last_lon, total;
    double cpd[3];

    make_track(&c, N);
    for (size_t i = 0; i < N; i++) {
        full[i] = c.tp[i].dist;
        full_t[i] = c.tp[i].t;
    }
    first_lat = c.tp[0].lat; first_lon = c.tp[0].lon;
    last_lat = c.tp[N - 1].lat; last_lon = c.tp[N - 1].lon;
    total = c.tp[N - 1].dist;

    /* Course points between trackpoints, at the ends, and in the middle. */
    cpd[0] = full[1234] + 1.3;
    cpd[1] = full[3001];
    cpd[2] = total;
    add_cp(&c, "A", cpd[0]);
    add_cp(&c, "B", cpd[1]);
    add_cp(&c, "C", cpd[2]);

    CHECK(course_decimate(&c, MAXP, &r) == 0);
    CHECK(c.n_tp == MAXP);
    CHECK(r.n_before == N && r.n_after == MAXP && !r.exceeded);
    CHECK(r.n_required == 4); /* first, last (= C's point), A's, B's */

    /* First and last kept, nothing invented, order and values preserved. */
    CHECK(c.tp[0].lat == first_lat && c.tp[0].lon == first_lon);
    CHECK(c.tp[c.n_tp - 1].lat == last_lat && c.tp[c.n_tp - 1].lon == last_lon);
    CHECK(c.tp[c.n_tp - 1].dist == total);
    for (size_t i = 1; i < c.n_tp; i++)
        CHECK(c.tp[i].dist > c.tp[i - 1].dist);
    for (size_t i = 0; i < c.n_tp; i++) {
        size_t k = nearest(full, N, c.tp[i].dist);
        CHECK(full[k] == c.tp[i].dist);      /* a real original point */
        CHECK(c.tp[i].t == full_t[k]);       /* with its original time */
    }

    /* The point nearest each course point survived, and the course point
     * distances are exactly the full-resolution values (so within 1 m). */
    for (int i = 0; i < 3; i++) {
        size_t k = nearest(full, N, cpd[i]);
        CHECK(has_dist(&c, full[k]));
        CHECK(c.cp[i].dist == cpd[i]);
    }

    /* The shape is kept: every dropped point lies within a modest distance
     * of the reduced line (a loose bound; the point is that it is not huge). */
    {
        struct course ref;
        double worst = 0.0;

        make_track(&ref, N);
        for (size_t k = 0; k < N; k++) {
            double best = 1e30;
            for (size_t i = 0; i + 1 < c.n_tp; i++) {
                struct seg_proj sp = project_to_segment(
                    ref.tp[k].lat, ref.tp[k].lon,
                    c.tp[i].lat, c.tp[i].lon,
                    c.tp[i + 1].lat, c.tp[i + 1].lon);
                if (sp.dist_m < best)
                    best = sp.dist_m;
            }
            if (best > worst)
                worst = best;
        }
        CHECK(worst < 25.0);
        course_free(&ref);
    }

    free(full);
    free(full_t);
    course_free(&c);
}

static void test_no_op(void)
{
    struct course c;
    struct decimate_report r;

    make_track(&c, 50);
    CHECK(course_decimate(&c, 50, &r) == 0);   /* exactly N */
    CHECK(c.n_tp == 50 && r.n_after == 50 && !r.exceeded);
    CHECK(course_decimate(&c, 500, &r) == 0);  /* fewer than N */
    CHECK(c.n_tp == 50);
    course_free(&c);
}

static void test_exceeded(void)
{
    struct course c;
    struct decimate_report r;

    make_track(&c, 1000);
    for (int i = 0; i < 10; i++)
        add_cp(&c, "P", c.tp[50 + 90 * i].dist);
    /* 10 course points + first + last = 12 required, budget 5. */
    CHECK(course_decimate(&c, 5, &r) == 0);
    CHECK(r.exceeded && r.n_required == 12 && r.n_after == 12 && c.n_tp == 12);
    for (int i = 0; i < 10; i++)
        CHECK(has_dist(&c, c.cp[i].dist));
    course_free(&c);
}

static void test_budget_equals_required(void)
{
    struct course c;
    struct decimate_report r;

    make_track(&c, 1000);
    add_cp(&c, "P", c.tp[400].dist);
    CHECK(course_decimate(&c, 3, &r) == 0);   /* first, last, one cp */
    CHECK(!r.exceeded && c.n_tp == 3 && r.n_required == 3);
    course_free(&c);
}

static void test_two_points_and_straight(void)
{
    struct course c;
    struct decimate_report r;

    make_track(&c, 1000);
    CHECK(course_decimate(&c, 2, &r) == 0);
    CHECK(c.n_tp == 2 && !r.exceeded);
    course_free(&c);

    /* A straight line with repeated points still reduces cleanly. */
    course_init(&c);
    for (int i = 0; i < 100; i++) {
        struct trackpoint tp = {0};
        tp.lat = 40.0;
        tp.lon = -100.0 + 0.001 * (i / 2);   /* every point twice */
        course_add_trackpoint(&c, &tp);
    }
    course_compute_distances(&c);
    CHECK(course_decimate(&c, 10, &r) == 0);
    CHECK(c.n_tp == 10);
    for (size_t i = 1; i < c.n_tp; i++)
        CHECK(c.tp[i].dist >= c.tp[i - 1].dist);
    course_free(&c);
}

int main(void)
{
    test_dense_reduction();
    test_no_op();
    test_exceeded();
    test_budget_equals_required();
    test_two_points_and_straight();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("test_decimate: all passed");
    return 0;
}
