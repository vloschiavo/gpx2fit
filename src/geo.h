/*
 * geo.h: geometry helpers for gpx2fit (phase 3).
 *
 * Units: degrees (WGS84 lat/lon), meters. Spherical Earth model with the
 * IUGG mean radius; error against the WGS84 ellipsoid is up to ~0.5%, far
 * below GPS noise for course purposes.
 */
#ifndef GEO_H
#define GEO_H

#include <stdbool.h>
#include <stddef.h>

#include "course.h"

#define EARTH_RADIUS_M 6371008.8

/* ---- 3.1 Distances ---------------------------------------------------- */

/* Great-circle distance between two points, meters. Symmetric; returns 0
 * for identical points. Handles the antimeridian. */
double haversine_m(double lat1, double lon1, double lat2, double lon2);

/* Set tp[i].dist to the cumulative distance from the first point, meters.
 * tp[0].dist = 0. Zero-length segments (repeated points) add nothing.
 * Safe for n_tp == 0 and n_tp == 1. */
void course_compute_distances(struct course *c);

/* ---- 3.2 Local projection --------------------------------------------- */

/*
 * Equirectangular projection to local x (east) / y (north) meters around a
 * reference point.
 *
 * Accuracy: x is scaled by cos(reference latitude), so the relative error of
 * an east-west distance at latitude phi is about tan(phi) * (phi - phi_ref)
 * (radians). Measured at 38.5N: 0.07% at 0.05 degrees (5.5 km) from the
 * reference latitude, 0.14% at 0.1 degrees, 0.63% at 0.45 degrees. North-south
 * distances are exact. Consequences:
 *   - Snapping builds the projection around the waypoint, where distances are
 *     a few hundred meters: error is ~0.003%. This is the intended use.
 *   - A 100 km chord through the reference point (any bearing, up to 55N) is
 *     within 0.1% (measured 0.0025%; see tests/test_geo.c).
 *   - Points far from the reference latitude, such as one end of a long
 *     east-west track away from the center, can exceed 0.1%. Use
 *     haversine_m() for track length; use this projection only for local work.
 * Not suitable near the poles.
 */
struct local_proj {
    double lat0;      /* reference latitude, degrees */
    double lon0;      /* reference longitude, degrees */
    double m_per_deg_lat; /* meters per degree of latitude */
    double m_per_deg_lon; /* meters per degree of longitude at lat0 */
};

void proj_init(struct local_proj *p, double lat0, double lon0);

/* lat/lon (degrees) -> x/y (meters). Longitude difference is wrapped to
 * [-180, 180] so the antimeridian is handled. */
void proj_forward(const struct local_proj *p, double lat, double lon,
                  double *x, double *y);

/* x/y (meters) -> lat/lon (degrees); lon is normalized to [-180, 180]. */
void proj_inverse(const struct local_proj *p, double x, double y,
                  double *lat, double *lon);

/* ---- 3.3 Point-to-segment projection ---------------------------------- */

struct seg_proj {
    double t;      /* position along the segment, clamped to [0, 1] */
    double lat;    /* projected (closest) point, degrees */
    double lon;
    double dist_m; /* distance from the query point to the projected point */
};

/* Project point P onto segment A-B. The local projection is centered on P,
 * so dist_m is accurate for points near the segment. A zero-length segment
 * (A == B) gives t = 0 and the projected point A. */
struct seg_proj project_to_segment(double plat, double plon,
                                   double alat, double alon,
                                   double blat, double blon);

/* ---- 3.4 Elevation statistics ----------------------------------------- */

struct ele_stats {
    bool   valid;   /* false if no trackpoint has elevation */
    double ascent;  /* total climb, meters (>= 0) */
    double descent; /* total descent, meters (>= 0, positive number) */
    double min_ele; /* meters; meaningful only if valid */
    double max_ele; /* meters; meaningful only if valid */
};

/*
 * Compute ascent/descent over points with has_ele, ignoring noise. Uses
 * hysteresis: a change counts only once the elevation has moved at least
 * threshold_m away from the last counted elevation, so slow steady climbs
 * are not lost the way they would be with a per-step threshold.
 * min/max use raw (unfiltered) elevations. Points without elevation are
 * skipped. A negative threshold is treated as 0.
 */
void course_elevation_stats(const struct course *c, double threshold_m,
                            struct ele_stats *out);

#endif /* GEO_H */
