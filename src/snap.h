/*
 * snap.h: snap GPX waypoints onto the track (phase 4).
 *
 * Units: degrees (WGS84 lat/lon), meters. All distances along the course use
 * tp[i].dist, so course_compute_distances() must have run first.
 */
#ifndef SNAP_H
#define SNAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "course.h"
#include "geo.h"

/*
 * Two local minima of the waypoint-to-track distance belong to the same pass
 * of the track unless the track climbs at least this far above the shallower
 * one in between (a "prominence" test). It stops GPS jitter on a single pass
 * from looking like several passes, while a real return leg, which leaves the
 * waypoint by far more than this, stays a separate pass.
 */
#define SNAP_PASS_SEP_M 10.0

/* ---- 4.3 to 4.5: pass selection modes ---------------------------------- */

enum snap_mode {
    SNAP_NEAREST = 0, /* candidate with the smallest offset (default) */
    SNAP_ORDERED,     /* distance along course never decreases in GPX order */
    SNAP_FIRST,       /* first pass of the track */
    SNAP_LAST         /* last pass of the track */
};

/* "nearest", "ordered", "first", "last" -> *out. Returns 0, or -1 if unknown. */
int snap_mode_parse(const char *s, enum snap_mode *out);
const char *snap_mode_name(enum snap_mode m);

/* ---- 4.1 Bounding-box prefilter ---------------------------------------- */

/* Bounding box of one segment tp[i]..tp[i+1]. The longitude interval is
 * stored as mid +/- half so that segments crossing the antimeridian work. */
struct seg_box {
    double lat_lo, lat_hi; /* degrees */
    double lon_mid;        /* degrees, may lie outside [-180, 180] */
    double lon_half;       /* degrees, >= 0 */
};

/* Built once per course; borrows the course (it must outlive the index and
 * its trackpoints must not change). A course with fewer than 2 trackpoints
 * has 0 segments. */
struct snap_index {
    const struct course *c;
    size_t               n_seg; /* n_tp - 1, or 0 */
    struct seg_box      *box;   /* n_seg entries; NULL if n_seg == 0 */
};

/* Returns 0, or -1 on allocation failure (index left empty). */
int  snap_index_init(struct snap_index *ix, const struct course *c);
void snap_index_free(struct snap_index *ix);

/*
 * Segments whose bounding box, grown by thr_m, contains the point. This is a
 * strict superset of the segments within thr_m of the point: no segment that
 * project_to_segment() would place within thr_m is ever dropped. Indices are
 * ascending (segment i is tp[i]..tp[i+1]). *segs is malloc'd and owned by the
 * caller (NULL when *n == 0). Returns 0, or -1 on allocation failure.
 */
int snap_prefilter(const struct snap_index *ix, double lat, double lon,
                   double thr_m, size_t **segs, size_t *n);

/* ---- 4.2 Candidate snaps for one waypoint ------------------------------ */

struct snap_candidate {
    size_t seg;    /* segment index; the point lies on tp[seg]..tp[seg+1] */
    double t;      /* position within that segment, 0..1 */
    double dist;   /* distance along the course, meters */
    double offset; /* distance from the waypoint to the track, meters */
    double lat;    /* projected position, degrees */
    double lon;
};

struct snap_cands {
    struct snap_candidate *v; /* in track order (ascending dist) */
    size_t                 n;
    size_t                 cap;
};

/*
 * Every local minimum of the distance from (lat, lon) to the track whose
 * offset is <= thr_m, one per pass of the track (see SNAP_PASS_SEP_M), in
 * track order. A waypoint next to the start or end of the track gets a
 * candidate at that end. Zero-length segments are ignored. On success *out is
 * filled (n may be 0) and must be released with snap_cands_free(). Returns 0,
 * or -1 on allocation failure.
 */
int  snap_candidates(const struct snap_index *ix, double lat, double lon,
                     double thr_m, struct snap_cands *out);
void snap_cands_free(struct snap_cands *cs);

/* Distance from the point to the nearest part of the whole track, meters
 * (brute force, O(n)). Used only for messages. Returns -1 for an empty
 * course. */
double snap_nearest_offset(const struct course *c, double lat, double lon);

/* ---- 4.3 to 4.6 Assignment, sorting, summary --------------------------- */

struct snap_options {
    enum snap_mode mode;
    double         threshold_m; /* --snap */
    bool           quiet;       /* no warnings on stderr */
    bool           strict;      /* nonzero result if any waypoint is skipped */
};

struct snap_report {
    size_t n_snapped;   /* course points created */
    size_t n_skipped;   /* waypoints with no candidate within the threshold */
    size_t n_unordered; /* ordered mode: waypoints that could not keep GPX order
                         * (warned about only if they had several passes) */
};

#define SNAP_OK      0
#define SNAP_NOMEM  -1
#define SNAP_STRICT  1 /* --strict and at least one waypoint was skipped */

/*
 * Snap every waypoint of c and replace c->cp with the result, sorted by
 * distance along the course (stable: equal distances keep GPX order).
 * Skipped waypoints warn on stderr unless quiet. Course points get type 0
 * (generic) and t = 0; both are filled in by later phases. The name is copied
 * untruncated (truncation is task 5.5). Returns SNAP_OK, SNAP_STRICT (course
 * points are still produced), or SNAP_NOMEM. *rep may be NULL.
 */
int snap_waypoints(struct course *c, const struct snap_options *opt,
                   struct snap_report *rep);

/* Stable sort of c->cp by dist. snap_waypoints() calls it. */
void snap_sort_course_points(struct course *c);

/* One line per course point (name, km, offset), then a tally line. */
void snap_print_summary(FILE *out, const struct course *c,
                        const struct snap_report *rep);

#endif /* SNAP_H */
