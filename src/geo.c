/* geo.c: geometry helpers for gpx2fit. See geo.h for units and accuracy. */
#include "geo.h"

#include <math.h>

#define PI_ 3.14159265358979323846
#define DEG2RAD (PI_ / 180.0)

/* Wrap a longitude difference into [-180, 180]. */
static double wrap_dlon(double d)
{
    while (d > 180.0)
        d -= 360.0;
    while (d < -180.0)
        d += 360.0;
    return d;
}

/* ---- 3.1 ---------------------------------------------------------------- */

double haversine_m(double lat1, double lon1, double lat2, double lon2)
{
    if (lat1 == lat2 && lon1 == lon2)
        return 0.0;

    double p1 = lat1 * DEG2RAD;
    double p2 = lat2 * DEG2RAD;
    double dp = p2 - p1;
    double dl = wrap_dlon(lon2 - lon1) * DEG2RAD;

    double s1 = sin(dp / 2.0);
    double s2 = sin(dl / 2.0);
    double a = s1 * s1 + cos(p1) * cos(p2) * s2 * s2;

    /* Rounding can push a slightly outside [0, 1] near antipodes. */
    if (a < 0.0)
        a = 0.0;
    if (a > 1.0)
        a = 1.0;

    return 2.0 * EARTH_RADIUS_M * atan2(sqrt(a), sqrt(1.0 - a));
}

void course_compute_distances(struct course *c)
{
    if (c->n_tp == 0)
        return;

    c->tp[0].dist = 0.0;
    for (size_t i = 1; i < c->n_tp; i++) {
        const struct trackpoint *a = &c->tp[i - 1];
        struct trackpoint *b = &c->tp[i];
        double d = 0.0;

        if (a->lat != b->lat || a->lon != b->lon)
            d = haversine_m(a->lat, a->lon, b->lat, b->lon);
        b->dist = a->dist + d;
    }
}

/* ---- 3.2 ---------------------------------------------------------------- */

void proj_init(struct local_proj *p, double lat0, double lon0)
{
    p->lat0 = lat0;
    p->lon0 = lon0;
    p->m_per_deg_lat = EARTH_RADIUS_M * DEG2RAD;
    p->m_per_deg_lon = EARTH_RADIUS_M * DEG2RAD * cos(lat0 * DEG2RAD);
}

void proj_forward(const struct local_proj *p, double lat, double lon,
                  double *x, double *y)
{
    *x = wrap_dlon(lon - p->lon0) * p->m_per_deg_lon;
    *y = (lat - p->lat0) * p->m_per_deg_lat;
}

void proj_inverse(const struct local_proj *p, double x, double y,
                  double *lat, double *lon)
{
    *lat = p->lat0 + y / p->m_per_deg_lat;
    /* m_per_deg_lon is ~0 only at the poles, which are out of scope. */
    *lon = wrap_dlon(p->lon0 + x / p->m_per_deg_lon);
}

/* ---- 3.3 ---------------------------------------------------------------- */

struct seg_proj project_to_segment(double plat, double plon,
                                   double alat, double alon,
                                   double blat, double blon)
{
    struct local_proj pr;
    struct seg_proj r;
    double ax, ay, bx, by;

    proj_init(&pr, plat, plon); /* P is the origin: px = py = 0 */
    proj_forward(&pr, alat, alon, &ax, &ay);
    proj_forward(&pr, blat, blon, &bx, &by);

    double dx = bx - ax;
    double dy = by - ay;
    double len2 = dx * dx + dy * dy;
    double t = 0.0;

    if (len2 > 0.0) {
        t = -(ax * dx + ay * dy) / len2; /* dot(P - A, B - A) / |B - A|^2 */
        if (t < 0.0)
            t = 0.0;
        if (t > 1.0)
            t = 1.0;
    }

    double qx = ax + t * dx;
    double qy = ay + t * dy;

    r.t = t;
    r.dist_m = sqrt(qx * qx + qy * qy);
    proj_inverse(&pr, qx, qy, &r.lat, &r.lon);
    return r;
}

/* ---- 3.4 ---------------------------------------------------------------- */

void course_elevation_stats(const struct course *c, double threshold_m,
                            struct ele_stats *out)
{
    bool have_ref = false;
    double ref = 0.0;

    if (threshold_m < 0.0)
        threshold_m = 0.0;

    out->valid = false;
    out->ascent = 0.0;
    out->descent = 0.0;
    out->min_ele = 0.0;
    out->max_ele = 0.0;

    for (size_t i = 0; i < c->n_tp; i++) {
        if (!c->tp[i].has_ele)
            continue;
        double e = c->tp[i].ele;

        if (!have_ref) {
            have_ref = true;
            ref = e;
            out->valid = true;
            out->min_ele = out->max_ele = e;
            continue;
        }

        if (e < out->min_ele)
            out->min_ele = e;
        if (e > out->max_ele)
            out->max_ele = e;

        double diff = e - ref;
        if (diff >= threshold_m && diff > 0.0) {
            out->ascent += diff;
            ref = e;
        } else if (-diff >= threshold_m && diff < 0.0) {
            out->descent += -diff;
            ref = e;
        }
    }
}
