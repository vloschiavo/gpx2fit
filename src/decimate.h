/*
 * decimate.h: reduce the number of trackpoints (task 8.2, --max-points).
 *
 * Units: meters, degrees. Needs tp[].dist (course_compute_distances()) and,
 * for the "keep the point nearest each course point" rule, c->cp[].dist
 * (snap_waypoints()).
 */
#ifndef DECIMATE_H
#define DECIMATE_H

#include <stdbool.h>
#include <stddef.h>

#include "course.h"

struct decimate_report {
    size_t n_before;   /* trackpoints before, count */
    size_t n_after;    /* trackpoints after, count */
    size_t n_required; /* points that must be kept: first, last, and the one
                        * nearest each course point (coinciding ones count
                        * once), count */
    bool   exceeded;   /* n_required > max_points: all required points were
                        * kept, so n_after > max_points */
};

/*
 * Reduce c->tp to at most max_points points (max_points >= 2), in place.
 *
 * Always kept: the first point, the last point, and for every course point
 * the trackpoint whose distance along the course is closest to cp.dist (the
 * lower one on a tie). The remaining budget is spent with Douglas-Peucker:
 * repeatedly keep the dropped point that lies farthest from the line joining
 * its two kept neighbours, until the budget is used or nothing is left to
 * add. So the result has exactly max_points points when there are more than
 * max_points, and a smaller input is left unchanged.
 *
 * If the required points alone exceed max_points they are all kept and
 * rep->exceeded is set; the caller decides whether that is a warning.
 *
 * Surviving points are not modified: tp[].dist and tp[].t keep their
 * full-resolution values, and course points (which refer to the track only by
 * distance) are not touched, so their distances and times do not change.
 * Elevation is not used for the shape test.
 *
 * Returns 0, or -1 on allocation failure (course unchanged). rep may be NULL.
 */
int course_decimate(struct course *c, size_t max_points,
                    struct decimate_report *rep);

#endif /* DECIMATE_H */
