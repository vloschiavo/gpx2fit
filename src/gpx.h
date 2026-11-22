/*
 * gpx.h: GPX 1.0 / 1.1 reader (expat).
 *
 * Reads one track (all of its segments joined) plus every waypoint into a
 * struct course. <sym> is deliberately not parsed; waypoint types are derived
 * from names later.
 */
#ifndef GPX_H
#define GPX_H

#include <stdbool.h>
#include <stddef.h>

#include "course.h"

struct gpx_options {
    long track;  /* 1-based <trk> index to use; 0 = pick the longest */
    bool quiet;  /* suppress warnings on stderr */
};

/* Read `path` into `c`, which must be freshly course_init()ed.
 *
 * Fills c->tp (lat, lon, ele/has_ele; dist and t are left 0), c->wp and
 * c->name (track <name>, else the file name without directory or extension).
 * Warnings (several tracks, several segments) go to stderr unless
 * opt->quiet.
 *
 * Returns 0 on success. On failure returns -1 and writes a one-line message
 * to err (truncated to errsz); the contents of *c are then unspecified but
 * still safe to pass to course_free(). */
int read_gpx(const char *path, struct course *c, const struct gpx_options *opt,
             char *err, size_t errsz);

/* Element-name helpers, exposed for unit tests. */

/* Strip a namespace from an element name. Understands expat's "uri local"
 * form (separator ' '), "{uri}local" and "prefix:local". Returns a pointer
 * into qname. */
const char *gpx_local_name(const char *qname);

/* True if qname is in no namespace or in a topografix.com GPX namespace
 * (any version), false for foreign namespaces such as Garmin extensions. */
bool gpx_ns_ok(const char *qname);

#endif /* GPX_H */
