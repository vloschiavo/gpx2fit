/*
 * timing.h: synthetic timestamps (phase 6).
 *
 * A GPX track from CalTopo has no <time>, but a FIT course wants a timestamp on
 * every record. We invent them from a start time and a constant pace. All times
 * here are seconds since the FIT epoch (1989-12-31 00:00:00 UTC), as uint32_t.
 *
 * Needs tp[i].dist (course_compute_distances) to have run first.
 */
#ifndef TIMING_H
#define TIMING_H

#include <stddef.h>
#include <stdint.h>

#include "course.h"

/* FIT epoch as Unix time: 1989-12-31 00:00:00 UTC = Unix 631065600. */
#define FIT_EPOCH_UNIX 631065600LL

/* The FIT protocol treats a date_time below 0x10000000 as a device uptime, not
 * a real date (that is 1998-07-03 21:24:16 UTC). 0xFFFFFFFF is the uint32
 * "invalid" value, so the last real time is 0xFFFFFFFE (2126-02-06). */
#define FIT_DATE_MIN 0x10000000u
#define FIT_TIME_MAX 0xFFFFFFFEu

/* Default start: 2025-01-01T00:00:00Z. A fixed date keeps output reproducible. */
#define DEFAULT_START_FIT 1104624000u

/* Default pace: 5 km/h, in m/s. */
#define DEFAULT_PACE_MPS (5.0 / 3.6)

/* Unix time of a UTC calendar date and time (proleptic Gregorian). The fields
 * are not range-checked beyond what the arithmetic needs. Does not use the C
 * library, so it does not depend on the time zone or on timegm(). */
int64_t unix_from_utc(int year, int month, int day, int hour, int min, int sec);

/*
 * --start. Accepts, in UTC only:
 *     YYYY-MM-DD
 *     YYYY-MM-DDTHH:MM[:SS][Z]
 * (all fields two digits except the year, which has four). Offsets such as
 * +02:00 are rejected. NULL gives DEFAULT_START_FIT. The result must lie in
 * [FIT_DATE_MIN, FIT_TIME_MAX]. Returns 0 and sets *fit_time, or -1 with a
 * short message in err.
 */
int parse_start(const char *text, uint32_t *fit_time, char *err, size_t err_size);

/*
 * --pace. Accepts either
 *     m/s:     DIGITS[.DIGITS]       e.g. 2.5
 *     min/km:  MIN:SS                e.g. 6:40   (SS is two digits, 00 to 59)
 * A value with a ':' is min/km, so a plain number is never read as minutes.
 * The speed must be greater than 0. NULL gives DEFAULT_PACE_MPS. Returns 0 and
 * sets *mps (meters per second), or -1 with a short message in err.
 */
int parse_pace(const char *text, double *mps, char *err, size_t err_size);

/* Return codes of course_assign_times(). */
#define TIMING_OK           0
#define TIMING_ARG         (-1) /* speed is not a finite number > 0 */
#define TIMING_TOOSHORT    (-2) /* fewer than 2 trackpoints */
#define TIMING_ZERO_LENGTH (-3) /* all trackpoints at the same distance */
#define TIMING_OVERFLOW    (-4) /* the last time would pass FIT_TIME_MAX */

/* One-line description of a TIMING_* code. */
const char *timing_strerror(int rc);

/*
 * Set tp[i].t = start + round(tp[i].dist / speed_mps), in whole seconds.
 * Equal distances (repeated points) get equal times and the times never
 * decrease. On any error nothing is modified.
 */
int course_assign_times(struct course *c, uint32_t start, double speed_mps);

/*
 * Set cp[i].t for every course point by linear interpolation of the times of
 * the two trackpoints on either side of its distance along the course, rounded
 * to a whole second (halves round up). A distance before the first point or
 * after the last point takes that point's time. Needs course_assign_times()
 * first. With no trackpoints every t is 0.
 */
void course_assign_cp_times(struct course *c);

#endif /* TIMING_H */
