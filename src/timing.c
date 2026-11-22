/* timing.c: synthetic timestamps (tasks 6.1, 6.2). */
#include "timing.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---- calendar ------------------------------------------------------------ */

/* Days from 1970-01-01 to the given civil date (Howard Hinnant's algorithm). */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d)
{
    int64_t era;
    unsigned yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

int64_t unix_from_utc(int year, int month, int day, int hour, int min, int sec)
{
    return days_from_civil(year, (unsigned)month, (unsigned)day) * 86400
           + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
}

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static int days_in_month(int y, int m)
{
    static const int dm[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    return (m == 2 && is_leap(y)) ? 29 : dm[m - 1];
}

/* ---- --start ------------------------------------------------------------- */

/* Read exactly n digits at *p into *out and advance. */
static bool digits(const char **p, int n, int *out)
{
    int v = 0;

    for (int i = 0; i < n; i++) {
        if ((*p)[i] < '0' || (*p)[i] > '9')
            return false;
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += n;
    *out = v;
    return true;
}

static void set_err(char *err, size_t n, const char *msg)
{
    if (err && n > 0)
        snprintf(err, n, "%s", msg);
}

int parse_start(const char *text, uint32_t *fit_time, char *err, size_t err_size)
{
    static const char *const shape =
        "expected YYYY-MM-DD or YYYY-MM-DDTHH:MM:SSZ (UTC)";
    const char *p = text;
    int y, mo, d, h = 0, mi = 0, s = 0;
    int64_t unix_t, fit;

    if (!text) {
        *fit_time = DEFAULT_START_FIT;
        return 0;
    }
    if (!digits(&p, 4, &y) || *p++ != '-' || !digits(&p, 2, &mo) ||
        *p++ != '-' || !digits(&p, 2, &d)) {
        set_err(err, err_size, shape);
        return -1;
    }
    if (*p == 'T') {
        p++;
        if (!digits(&p, 2, &h) || *p++ != ':' || !digits(&p, 2, &mi)) {
            set_err(err, err_size, shape);
            return -1;
        }
        if (*p == ':') {
            p++;
            if (!digits(&p, 2, &s)) {
                set_err(err, err_size, shape);
                return -1;
            }
        }
        if (*p == 'Z')
            p++;
    }
    if (*p == '+' || *p == '-') {
        set_err(err, err_size, "time zone offsets are not supported, use UTC "
                "(end the time with Z)");
        return -1;
    }
    if (*p != '\0') {
        set_err(err, err_size, shape);
        return -1;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > days_in_month(y, mo)) {
        set_err(err, err_size, "not a valid calendar date");
        return -1;
    }
    if (h > 23 || mi > 59 || s > 59) {
        set_err(err, err_size, "not a valid time of day");
        return -1;
    }

    unix_t = unix_from_utc(y, mo, d, h, mi, s);
    fit = unix_t - FIT_EPOCH_UNIX;
    if (fit < (int64_t)FIT_DATE_MIN) {
        set_err(err, err_size, "too early: FIT dates start at "
                "1998-07-03T21:24:16Z");
        return -1;
    }
    if (fit > (int64_t)FIT_TIME_MAX) {
        set_err(err, err_size, "too late: FIT dates end at 2126-02-06T06:28:14Z");
        return -1;
    }
    *fit_time = (uint32_t)fit;
    return 0;
}

/* ---- --pace -------------------------------------------------------------- */

/* Read one or more digits as an integer. Returns the count of digits read
 * (0 if none, or if the value would be unreasonably large: more than 9). */
static int read_uint(const char **p, double *out)
{
    int n = 0;
    double v = 0.0;

    while ((*p)[n] >= '0' && (*p)[n] <= '9') {
        v = v * 10.0 + ((*p)[n] - '0');
        n++;
        if (n > 9)
            return 0;
    }
    *p += n;
    *out = v;
    return n;
}

int parse_pace(const char *text, double *mps, char *err, size_t err_size)
{
    static const char *const shape =
        "expected m/s such as 2.5, or min/km such as 6:40";
    const char *p = text;
    double a, b, speed;

    if (!text) {
        *mps = DEFAULT_PACE_MPS;
        return 0;
    }
    if (!read_uint(&p, &a)) {
        set_err(err, err_size, shape);
        return -1;
    }
    if (*p == ':') {                          /* min/km: MIN:SS */
        const char *q = ++p;

        if (!(q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9') ||
            q[2] != '\0') {
            set_err(err, err_size, shape);
            return -1;
        }
        b = (q[0] - '0') * 10 + (q[1] - '0');
        if (b > 59) {
            set_err(err, err_size, "seconds must be 00 to 59 in a min/km pace");
            return -1;
        }
        if (a * 60.0 + b <= 0.0) {
            set_err(err, err_size, "pace must be greater than 0:00");
            return -1;
        }
        speed = 1000.0 / (a * 60.0 + b);
    } else {                                  /* m/s: DIGITS[.DIGITS] */
        speed = a;
        if (*p == '.') {
            double scale = 0.1;
            int nf = 0;

            p++;
            while (*p >= '0' && *p <= '9') {
                speed += (*p - '0') * scale;
                scale /= 10.0;
                p++;
                nf++;
            }
            if (nf == 0) {
                set_err(err, err_size, shape);
                return -1;
            }
        }
        if (*p != '\0') {
            set_err(err, err_size, shape);
            return -1;
        }
        if (speed <= 0.0) {
            set_err(err, err_size, "speed must be greater than 0");
            return -1;
        }
    }
    *mps = speed;
    return 0;
}

/* ---- assigning times ----------------------------------------------------- */

const char *timing_strerror(int rc)
{
    switch (rc) {
    case TIMING_OK:          return "ok";
    case TIMING_ARG:         return "pace is not a positive finite speed";
    case TIMING_TOOSHORT:    return "the track has fewer than 2 points";
    case TIMING_ZERO_LENGTH: return "the track has zero length";
    case TIMING_OVERFLOW:    return "the course would end after the last "
                                    "date a FIT file can hold (2126); use a "
                                    "faster --pace or an earlier --start";
    default:                 return "unknown error";
    }
}

/* Whole seconds from the start for a distance; dist < 0 counts as 0. */
static double elapsed(double dist, double speed)
{
    return dist > 0.0 ? dist / speed : 0.0;
}

int course_assign_times(struct course *c, uint32_t start, double speed_mps)
{
    double limit = (double)(FIT_TIME_MAX - start);
    double top = 0.0;
    uint32_t prev;

    if (!isfinite(speed_mps) || speed_mps <= 0.0 || start > FIT_TIME_MAX)
        return TIMING_ARG;
    if (c->n_tp < 2)
        return TIMING_TOOSHORT;

    /* First pass: validate everything, so a failure modifies nothing. */
    for (size_t i = 0; i < c->n_tp; i++) {
        double s = round(elapsed(c->tp[i].dist, speed_mps));

        if (!(s <= limit))     /* also catches NaN and infinity */
            return TIMING_OVERFLOW;
        if (s > top)
            top = s;
    }
    if (top == 0.0 && !(c->tp[c->n_tp - 1].dist > 0.0))
        return TIMING_ZERO_LENGTH;

    /* Second pass: write. The running maximum keeps times from ever
     * decreasing, even if the distances were not monotonic. */
    prev = start;
    for (size_t i = 0; i < c->n_tp; i++) {
        uint32_t t = start + (uint32_t)round(elapsed(c->tp[i].dist, speed_mps));

        if (t < prev)
            t = prev;
        c->tp[i].t = t;
        prev = t;
    }
    return TIMING_OK;
}

/* Index of the last trackpoint with dist <= d; n_tp if d is before the first. */
static size_t last_at_or_before(const struct course *c, double d)
{
    size_t lo = 0, hi = c->n_tp; /* invariant: answer in [lo - 1, hi) */

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;

        if (c->tp[mid].dist <= d)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo == 0 ? c->n_tp : lo - 1;
}

void course_assign_cp_times(struct course *c)
{
    for (size_t i = 0; i < c->n_cp; i++) {
        struct course_pt *p = &c->cp[i];
        double d = p->dist;
        size_t j;

        if (c->n_tp == 0) {
            p->t = 0;
            continue;
        }
        if (!(d > c->tp[0].dist))      /* before or at the start, or NaN */
            j = 0;
        else
            j = last_at_or_before(c, d);
        if (j >= c->n_tp)
            j = 0;

        if (j + 1 >= c->n_tp || !(d > c->tp[j].dist)) {
            /* Last point or beyond, or exactly on trackpoint j. */
            p->t = c->tp[j].t;
        } else {
            /* tp[j].dist <= d < tp[j + 1].dist, so the span is > 0. */
            double span = c->tp[j + 1].dist - c->tp[j].dist;
            double f = (d - c->tp[j].dist) / span;
            double dt = (double)c->tp[j + 1].t - (double)c->tp[j].t;

            p->t = (uint32_t)((double)c->tp[j].t + floor(f * dt + 0.5));
        }
    }
}
