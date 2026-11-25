/*
 * course.h: core data types for gpx2fit.
 *
 * Units used throughout:
 *   - angles (lat/lon): decimal degrees, WGS84 (lat -90..90, lon -180..180)
 *   - distances and elevations: meters
 *   - times: seconds since the FIT epoch (1989-12-31 00:00:00 UTC)
 */
#ifndef COURSE_H
#define COURSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One point of the track, in file order. */
struct trackpoint {
    double   lat;     /* latitude, degrees */
    double   lon;     /* longitude, degrees */
    double   ele;     /* elevation, meters; meaningful only if has_ele */
    bool     has_ele; /* true only if the GPX point had an <ele> element */
    double   dist;    /* cumulative distance from the first point, meters */
    uint32_t t;       /* synthetic timestamp, seconds since FIT epoch */
};

/* A waypoint as read from the GPX file. No <sym> is kept: the type is
 * derived from the name later (see symmap). */
struct waypoint {
    char  *name; /* heap-allocated UTF-8, NUL-terminated; NULL if unnamed */
    double lat;  /* latitude, degrees */
    double lon;  /* longitude, degrees */
};

/* A waypoint after snapping to the track; becomes a FIT course_point. With
 * --pass both a waypoint visited several times yields one of these per pass,
 * each with the same name but its own distance along the course. */
struct course_pt {
    char    *name;   /* heap-allocated UTF-8, NUL-terminated, already truncated */
    double   lat;    /* snapped (projected) latitude, degrees */
    double   lon;    /* snapped (projected) longitude, degrees */
    double   dist;   /* distance along the course, meters */
    double   offset; /* distance from the original waypoint to the track, meters */
    uint32_t t;      /* interpolated timestamp, seconds since FIT epoch */
    uint8_t  type;   /* FIT course_point type enum value (generic = 0) */
    size_t   wp_index; /* index of the source waypoint in c->wp (UI status key) */
};

/* The whole course. Each array is growable: n_* items used, cap_* allocated. */
struct course {
    char              *name;   /* heap-allocated UTF-8 course name; NULL if unset */

    struct trackpoint *tp;     /* trackpoints in track order */
    size_t             n_tp;   /* number of trackpoints in use (count) */
    size_t             cap_tp; /* allocated trackpoint slots (count) */

    struct waypoint   *wp;     /* waypoints in GPX order */
    size_t             n_wp;   /* number of waypoints in use (count) */
    size_t             cap_wp; /* allocated waypoint slots (count) */

    struct course_pt  *cp;     /* course points, sorted by dist once snapped */
    size_t             n_cp;   /* number of course points in use (count) */
    size_t             cap_cp; /* allocated course point slots (count) */
};

/* Set every field to empty/zero. Does not free; call on uninitialized memory. */
void course_init(struct course *c);

/* Free all owned memory (name strings and arrays), then reset to the empty
 * state so a second call, or course_init-style reuse, is safe. */
void course_free(struct course *c);

/* Append a trackpoint (copied by value), growing the array as needed.
 * Returns 0 on success, -1 on allocation failure (course left unchanged). */
int course_add_trackpoint(struct course *c, const struct trackpoint *tp);

/* Append a waypoint. The name is copied (NULL allowed); lat/lon in degrees.
 * Returns 0 on success, -1 on allocation failure (course left unchanged). */
int course_add_waypoint(struct course *c, const char *name,
                        double lat, double lon);

#endif /* COURSE_H */
