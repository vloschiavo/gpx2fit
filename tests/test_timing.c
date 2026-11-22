/* test_timing.c: tests for src/timing.c (tasks 6.1, 6.2). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/timing.h"

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

/* A course with n trackpoints whose dist values are given; no other fields. */
static void make_course(struct course *c, const double *dist, size_t n)
{
    memset(c, 0, sizeof *c);
    c->tp = calloc(n ? n : 1, sizeof *c->tp);
    c->n_tp = c->cap_tp = n;
    for (size_t i = 0; i < n; i++) {
        c->tp[i].dist = dist[i];
        c->tp[i].t = 0xDEADBEEFu; /* must be overwritten (or kept on error) */
    }
}

static void add_cps(struct course *c, const double *dist, size_t n)
{
    c->cp = calloc(n ? n : 1, sizeof *c->cp);
    c->n_cp = c->cap_cp = n;
    for (size_t i = 0; i < n; i++) {
        c->cp[i].dist = dist[i];
        c->cp[i].t = 0xDEADBEEFu;
    }
}

static void free_course(struct course *c)
{
    free(c->tp);
    free(c->cp);
}

/* ---- 6.1: epoch and calendar -------------------------------------------- */

static void test_epoch(void)
{
    /* The epoch constant: FIT epoch = Unix epoch + 631065600 s. */
    CHECK(FIT_EPOCH_UNIX == 631065600LL);
    CHECK(unix_from_utc(1970, 1, 1, 0, 0, 0) == 0);
    CHECK(unix_from_utc(1989, 12, 31, 0, 0, 0) == FIT_EPOCH_UNIX);

    /* Known dates, including leap days. */
    CHECK(unix_from_utc(2000, 1, 1, 0, 0, 0) == 946684800LL);
    CHECK(unix_from_utc(2000, 2, 29, 0, 0, 0) == 951782400LL);
    CHECK(unix_from_utc(2024, 2, 29, 0, 0, 0) == 1709164800LL);
    CHECK(unix_from_utc(2025, 1, 1, 0, 0, 0) == 1735689600LL);
    CHECK(unix_from_utc(2025, 6, 28, 8, 0, 0) == 1751097600LL);
    CHECK(unix_from_utc(2025, 1, 1, 23, 59, 59) == 1735689600LL + 86399);
    CHECK(unix_from_utc(2025, 3, 1, 0, 0, 0) - unix_from_utc(2025, 2, 28, 0, 0, 0)
          == 86400);                      /* 2025 is not a leap year */
    CHECK(unix_from_utc(2024, 3, 1, 0, 0, 0) - unix_from_utc(2024, 2, 28, 0, 0, 0)
          == 2 * 86400);                  /* 2024 is */
    CHECK(unix_from_utc(1969, 12, 31, 23, 59, 59) == -1);

    /* The default start is 2025-01-01T00:00:00Z in FIT seconds. */
    CHECK((int64_t)DEFAULT_START_FIT ==
          unix_from_utc(2025, 1, 1, 0, 0, 0) - FIT_EPOCH_UNIX);
    CHECK(DEFAULT_START_FIT >= FIT_DATE_MIN && DEFAULT_START_FIT <= FIT_TIME_MAX);

    /* Limits of the FIT range, as given in the header comment. */
    CHECK((int64_t)FIT_DATE_MIN + FIT_EPOCH_UNIX ==
          unix_from_utc(1998, 7, 3, 21, 24, 16));
    CHECK((int64_t)FIT_TIME_MAX + FIT_EPOCH_UNIX ==
          unix_from_utc(2126, 2, 6, 6, 28, 14));
}

/* ---- --start ------------------------------------------------------------ */

static uint32_t start_ok(const char *s)
{
    uint32_t v = 0;
    char err[128];

    CHECK(parse_start(s, &v, err, sizeof err) == 0);
    return v;
}

static void start_bad(const char *s, const char *msg_part)
{
    uint32_t v = 12345;
    char err[128] = "";

    CHECK(parse_start(s, &v, err, sizeof err) == -1);
    CHECK(err[0] != '\0');
    if (msg_part)
        CHECK(strstr(err, msg_part) != NULL);
    if (err[0] == '\0' || (msg_part && !strstr(err, msg_part)))
        fprintf(stderr, "  (input '%s', message '%s')\n", s, err);
}

static void test_parse_start(void)
{
    const uint32_t jun28 = 1120032000u; /* 2025-06-28T08:00:00Z */
    uint32_t v = 0;

    /* NULL means the default. */
    CHECK(parse_start(NULL, &v, NULL, 0) == 0 && v == DEFAULT_START_FIT);

    CHECK(start_ok("2025-06-28T08:00:00Z") == jun28);
    CHECK(start_ok("2025-06-28T08:00:00") == jun28);    /* Z optional */
    CHECK(start_ok("2025-06-28T08:00Z") == jun28);      /* seconds optional */
    CHECK(start_ok("2025-06-28T08:00") == jun28);
    CHECK(start_ok("2025-06-28T08:00:07Z") == jun28 + 7);
    CHECK(start_ok("2025-06-28T23:59:59Z") ==
          jun28 + 15 * 3600 + 59 * 60 + 59);            /* 08:00 + 15:59:59 */
    CHECK(start_ok("2025-06-28") == jun28 - 8 * 3600);  /* date only: midnight */
    CHECK(start_ok("2025-01-01T00:00:00Z") == DEFAULT_START_FIT);
    CHECK(start_ok("2024-02-29T12:00:00Z") ==
          (uint32_t)(1709164800LL + 43200 - FIT_EPOCH_UNIX));  /* leap day */

    /* Range edges. */
    CHECK(start_ok("1998-07-03T21:24:16Z") == FIT_DATE_MIN);
    start_bad("1998-07-03T21:24:15Z", "too early");
    CHECK(start_ok("2126-02-06T06:28:14Z") == FIT_TIME_MAX);
    start_bad("2126-02-06T06:28:15Z", "too late");
    start_bad("1989-12-31T00:00:00Z", "too early");      /* the FIT epoch itself */
    start_bad("1970-01-01", "too early");
    start_bad("2200-01-01", "too late");

    /* Time zone offsets are refused, not guessed. */
    start_bad("2025-06-28T08:00:00+02:00", "UTC");
    start_bad("2025-06-28T08:00:00-05:00", "UTC");
    start_bad("2025-06-28T08:00+0200", "UTC");

    /* Invalid calendar and clock values. */
    start_bad("2025-13-01", "calendar");
    start_bad("2025-00-10", "calendar");
    start_bad("2025-04-31", "calendar");
    start_bad("2025-02-29", "calendar");                /* not a leap year */
    start_bad("2100-02-29", "calendar");                /* century, not leap */
    CHECK(start_ok("2000-02-29") ==
          (uint32_t)(951782400LL - FIT_EPOCH_UNIX));    /* century, leap (400) */
    start_bad("2025-06-28T24:00:00Z", "time of day");
    start_bad("2025-06-28T08:60:00Z", "time of day");
    start_bad("2025-06-28T08:00:60Z", "time of day");   /* no leap seconds */

    /* Malformed. */
    start_bad("", "expected");
    start_bad("yesterday", "expected");
    start_bad("2025-6-28", "expected");
    start_bad("25-06-28", "expected");
    start_bad("2025/06/28", "expected");
    start_bad("2025-06-28 08:00:00", "expected");       /* space, not 'T' */
    start_bad("2025-06-28t08:00:00z", "expected");      /* lower case */
    start_bad("2025-06-28T8:00:00Z", "expected");
    start_bad("2025-06-28T08", "expected");
    start_bad("2025-06-28T08:00:00.5Z", "expected");
    start_bad("2025-06-28T08:00:00Zjunk", "expected");
    start_bad("2025-06-28Z", "expected");
    start_bad("2025-06-28T08:00:00ZZ", "expected");

    /* err may be NULL / size 0 without crashing. */
    CHECK(parse_start("nope", &v, NULL, 0) == -1);
}

/* ---- --pace ------------------------------------------------------------- */

static void pace_ok(const char *s, double want)
{
    double v = 0.0;
    char err[128];

    CHECK(parse_pace(s, &v, err, sizeof err) == 0);
    CHECK_NEAR(v, want, 1e-9);
}

static void pace_bad(const char *s)
{
    double v = 7.0;
    char err[128] = "";

    CHECK(parse_pace(s, &v, err, sizeof err) == -1);
    CHECK(err[0] != '\0');
    if (err[0] == '\0')
        fprintf(stderr, "  (input '%s')\n", s);
}

static void test_parse_pace(void)
{
    double v = 0.0;

    CHECK(parse_pace(NULL, &v, NULL, 0) == 0);
    CHECK_NEAR(v, 5.0 / 3.6, 1e-12);                    /* 5 km/h */

    /* m/s */
    pace_ok("2.5", 2.5);
    pace_ok("2", 2.0);
    pace_ok("0.5", 0.5);
    pace_ok("1.25", 1.25);
    pace_ok("10", 10.0);
    pace_ok("007", 7.0);
    pace_ok("1.3888889", 1.3888889);

    /* min/km: a ':' means minutes per kilometer */
    pace_ok("6:40", 1000.0 / 400.0);                    /* 2.5 m/s */
    pace_ok("5:00", 1000.0 / 300.0);
    pace_ok("10:00", 1000.0 / 600.0);
    pace_ok("12:30", 1000.0 / 750.0);
    pace_ok("0:30", 1000.0 / 30.0);
    pace_ok("0:01", 1000.0);
    pace_ok("99:59", 1000.0 / (99 * 60 + 59));
    pace_ok("6:00", 1000.0 / 360.0);
    /* "6:40" is NOT 6.4 m/s and "2.5" is NOT 2.5 min/km. */
    CHECK(fabs(1000.0 / 400.0 - 6.4) > 1.0);

    /* Zero, negative, non-numeric. */
    pace_bad("0");
    pace_bad("0.0");
    pace_bad("0:00");
    pace_bad("00:00");
    pace_bad("-2");
    pace_bad("+2");
    pace_bad("-6:40");
    pace_bad("");
    pace_bad("fast");
    pace_bad("abc");
    pace_bad("nan");
    pace_bad("inf");
    pace_bad("0x10");
    pace_bad("1e3");
    pace_bad("2.5.1");
    pace_bad(".5");
    pace_bad("2.");
    pace_bad(" 2.5");
    pace_bad("2.5 ");
    pace_bad("2,5");
    pace_bad("2.5m/s");

    /* Bad min/km. */
    pace_bad("6:");
    pace_bad(":40");
    pace_bad("6:5");                                    /* two digits needed */
    pace_bad("6:060");
    pace_bad("6:60");
    pace_bad("6:75");
    pace_bad("6:40:00");
    pace_bad("6:40.5");
    pace_bad("6:4x");
    pace_bad("6:-4");

    /* Absurdly long numbers are refused rather than overflowing. */
    pace_bad("12345678901234567890");
    pace_bad("12345678901234567890:00");
}

/* ---- 6.1: assigning times ------------------------------------------------ */

static void test_assign_times(void)
{
    struct course c;
    const double dist[] = { 0, 0, 100, 100, 250, 1000 };    /* with repeats */
    const uint32_t start = 1120032000u;
    size_t n = sizeof dist / sizeof *dist;

    make_course(&c, dist, n);
    CHECK(course_assign_times(&c, start, 2.0) == TIMING_OK);
    CHECK(c.tp[0].t == start);                      /* first point = start */
    CHECK(c.tp[1].t == start);                      /* duplicate: same time */
    CHECK(c.tp[2].t == start + 50);
    CHECK(c.tp[3].t == c.tp[2].t);                  /* duplicate: same time */
    CHECK(c.tp[4].t == start + 125);
    CHECK(c.tp[5].t == start + 500);
    for (size_t i = 1; i < n; i++)
        CHECK(c.tp[i].t >= c.tp[i - 1].t);          /* monotonic */
    free_course(&c);

    /* Rounding to the nearest second: 1 m at 2 m/s = 0.5 s rounds to 1. */
    {
        const double d[] = { 0, 1, 2, 3 };

        make_course(&c, d, 4);
        CHECK(course_assign_times(&c, start, 2.0) == TIMING_OK);
        CHECK(c.tp[1].t == start + 1);
        CHECK(c.tp[2].t == start + 1);
        CHECK(c.tp[3].t == start + 2);              /* 1.5 -> 2 */
        free_course(&c);
    }

    /* A realistic pace: 4126.89 m (the reference course) at 6:40 min/km. */
    {
        const double d[] = { 0, 1000, 4126.89 };
        double mps = 0.0;

        CHECK(parse_pace("6:40", &mps, NULL, 0) == 0);
        make_course(&c, d, 3);
        CHECK(course_assign_times(&c, start, mps) == TIMING_OK);
        CHECK(c.tp[1].t == start + 400);
        CHECK(c.tp[2].t == start + (uint32_t)llround(4126.89 / 2.5));
        free_course(&c);
    }

    /* Monotonic over many points, including long runs of repeats. */
    {
        double d[500];
        double acc = 0.0;

        for (int i = 0; i < 500; i++) {
            if (i % 7 != 0)
                acc += (i % 13) * 3.7;              /* some steps are 0 */
            d[i] = acc;
        }
        make_course(&c, d, 500);
        CHECK(course_assign_times(&c, start, 1.4) == TIMING_OK);
        CHECK(c.tp[0].t == start);
        for (int i = 1; i < 500; i++) {
            CHECK(c.tp[i].t >= c.tp[i - 1].t);
            if (d[i] == d[i - 1])
                CHECK(c.tp[i].t == c.tp[i - 1].t);
        }
        free_course(&c);
    }

    /* Non-monotonic distances (a bug elsewhere) still give monotonic times. */
    {
        const double d[] = { 0, 100, 50, 200 };

        make_course(&c, d, 4);
        CHECK(course_assign_times(&c, start, 1.0) == TIMING_OK);
        CHECK(c.tp[2].t >= c.tp[1].t && c.tp[3].t >= c.tp[2].t);
        free_course(&c);
    }
}

static void test_assign_times_errors(void)
{
    struct course c;
    const double ok[] = { 0, 100 };
    const double zero[] = { 0, 0, 0 };
    const uint32_t start = DEFAULT_START_FIT;

    /* Bad speeds. */
    make_course(&c, ok, 2);
    CHECK(course_assign_times(&c, start, 0.0) == TIMING_ARG);
    CHECK(course_assign_times(&c, start, -1.0) == TIMING_ARG);
    CHECK(course_assign_times(&c, start, NAN) == TIMING_ARG);
    CHECK(course_assign_times(&c, start, INFINITY) == TIMING_ARG);
    CHECK(c.tp[0].t == 0xDEADBEEFu && c.tp[1].t == 0xDEADBEEFu); /* untouched */
    CHECK(course_assign_times(&c, FIT_TIME_MAX + 1u, 1.0) == TIMING_ARG);
    free_course(&c);

    /* Too short. */
    make_course(&c, ok, 0);
    CHECK(course_assign_times(&c, start, 1.0) == TIMING_TOOSHORT);
    free_course(&c);
    make_course(&c, ok, 1);
    CHECK(course_assign_times(&c, start, 1.0) == TIMING_TOOSHORT);
    CHECK(c.tp[0].t == 0xDEADBEEFu);
    free_course(&c);

    /* Zero length. */
    make_course(&c, zero, 3);
    CHECK(course_assign_times(&c, start, 1.0) == TIMING_ZERO_LENGTH);
    CHECK(c.tp[0].t == 0xDEADBEEFu);
    free_course(&c);

    /* Overflow of the uint32 range: the boundary itself is allowed. */
    make_course(&c, ok, 2);
    CHECK(course_assign_times(&c, FIT_TIME_MAX - 100, 1.0) == TIMING_OK);
    CHECK(c.tp[1].t == FIT_TIME_MAX);
    free_course(&c);

    make_course(&c, ok, 2);
    CHECK(course_assign_times(&c, FIT_TIME_MAX - 99, 1.0) == TIMING_OVERFLOW);
    CHECK(c.tp[0].t == 0xDEADBEEFu && c.tp[1].t == 0xDEADBEEFu); /* untouched */
    CHECK(course_assign_times(&c, start, 1e-300) == TIMING_OVERFLOW); /* huge */
    CHECK(strlen(timing_strerror(TIMING_OVERFLOW)) > 10);
    free_course(&c);

    for (int rc = TIMING_OVERFLOW; rc <= TIMING_OK; rc++)
        CHECK(timing_strerror(rc)[0] != '\0');
}

/* ---- 6.2: course point times -------------------------------------------- */

static void test_cp_times(void)
{
    struct course c;
    const double dist[] = { 0, 100, 300 };
    const uint32_t start = 1120032000u;

    make_course(&c, dist, 3);
    CHECK(course_assign_times(&c, start, 1.0) == TIMING_OK);  /* t = start+dist */

    {
        const double cp[] = { 0, 50, 100, 200, 250, 300 };

        add_cps(&c, cp, 6);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start);                  /* at the first point */
        CHECK(c.cp[1].t == start + 50);             /* halfway, 1st segment */
        CHECK(c.cp[2].t == start + 100);            /* exactly on a vertex */
        CHECK(c.cp[3].t == start + 200);            /* halfway, 2nd segment */
        CHECK(c.cp[4].t == start + 250);
        CHECK(c.cp[5].t == start + 300);            /* at the last point */
        free(c.cp);
        c.cp = NULL;
        c.n_cp = 0;
    }

    /* Beyond the ends: clamp. NaN: first point. */
    {
        const double cp[] = { -5, 300.0001, 1e9, NAN };

        add_cps(&c, cp, 4);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start);
        CHECK(c.cp[1].t == start + 300);
        CHECK(c.cp[2].t == start + 300);
        CHECK(c.cp[3].t == start);
        free(c.cp);
        c.cp = NULL;
        c.n_cp = 0;
    }
    free_course(&c);

    /* The midpoint of a segment at a pace that is not 1 m/s. */
    {
        const double d[] = { 0, 400, 1000 };
        const double cp[] = { 200, 700 };

        make_course(&c, d, 3);
        CHECK(course_assign_times(&c, start, 2.5) == TIMING_OK);  /* 6:40 */
        add_cps(&c, cp, 2);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start + 80);             /* midpoint of 0..160 s */
        CHECK(c.cp[1].t == start + 280);            /* midpoint of 160..400 */
        CHECK(c.cp[0].t == (c.tp[0].t + c.tp[1].t) / 2);
        CHECK(c.cp[1].t == (c.tp[1].t + c.tp[2].t) / 2);
        free_course(&c);
    }

    /* Fractions round to the nearest second; exact halves round up. */
    {
        const double d[] = { 0, 30 };
        const double cp[] = { 10, 15, 20 };

        make_course(&c, d, 2);
        CHECK(course_assign_times(&c, start, 3.0) == TIMING_OK);  /* 0..10 s */
        add_cps(&c, cp, 3);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start + 3);              /* 3.33 -> 3 */
        CHECK(c.cp[1].t == start + 5);              /* 5.0 */
        CHECK(c.cp[2].t == start + 7);              /* 6.67 -> 7 */
        free_course(&c);

        const double d2[] = { 0, 30 };
        const double cp2[] = { 15 };

        make_course(&c, d2, 2);
        CHECK(course_assign_times(&c, start, 30.0) == TIMING_OK); /* 0..1 s */
        add_cps(&c, cp2, 1);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start + 1);              /* 0.5 -> 1 */
        free_course(&c);
    }

    /* Repeated trackpoints: a course point on the repeat gets its time, and
     * points on either side interpolate across the real segments. */
    {
        const double d[] = { 0, 100, 100, 100, 200 };
        const double cp[] = { 50, 100, 150 };

        make_course(&c, d, 5);
        CHECK(course_assign_times(&c, start, 1.0) == TIMING_OK);
        add_cps(&c, cp, 3);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start + 50);
        CHECK(c.cp[1].t == start + 100);
        CHECK(c.cp[2].t == start + 150);
        free_course(&c);
    }

    /* Repeats at the very start: first point's time. */
    {
        const double d[] = { 0, 0, 0, 100 };
        const double cp[] = { 0, 50 };

        make_course(&c, d, 4);
        CHECK(course_assign_times(&c, start, 1.0) == TIMING_OK);
        add_cps(&c, cp, 2);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == start);
        CHECK(c.cp[1].t == start + 50);
        free_course(&c);
    }

    /* Sweep: course point times never decrease with distance and stay inside
     * the bracketing trackpoints. */
    {
        double d[200], cp[400];
        double acc = 0.0;
        size_t k = 0;

        for (int i = 0; i < 200; i++) {
            acc += (i % 5 == 0) ? 0.0 : 3.0 + (i % 11) * 4.1;
            d[i] = acc;
        }
        for (double x = 0.0; x <= acc + 20.0 && k < 400; x += acc / 390.0)
            cp[k++] = x;
        make_course(&c, d, 200);
        CHECK(course_assign_times(&c, start, 1.37) == TIMING_OK);
        add_cps(&c, cp, k);
        course_assign_cp_times(&c);
        for (size_t i = 0; i < k; i++) {
            CHECK(c.cp[i].t >= c.tp[0].t && c.cp[i].t <= c.tp[199].t);
            if (i)
                CHECK(c.cp[i].t >= c.cp[i - 1].t);
        }
        free_course(&c);
    }

    /* No trackpoints, and one trackpoint. */
    {
        const double cp[] = { 10 };

        make_course(&c, NULL, 0);
        add_cps(&c, cp, 1);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == 0);
        free_course(&c);

        const double d1[] = { 0 };

        make_course(&c, d1, 1);
        c.tp[0].t = 4242;
        add_cps(&c, cp, 1);
        course_assign_cp_times(&c);
        CHECK(c.cp[0].t == 4242);
        free_course(&c);
    }

    /* No course points: nothing to do. */
    make_course(&c, dist, 3);
    course_assign_cp_times(&c);
    free_course(&c);
}

int main(void)
{
    test_epoch();
    test_parse_start();
    test_parse_pace();
    test_assign_times();
    test_assign_times_errors();
    test_cp_times();

    if (failures) {
        fprintf(stderr, "test_timing: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_timing: all %d checks passed\n", checks);
    return 0;
}
