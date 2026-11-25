/*
 * version.h: the single authoritative version string for gpx2fit.
 *
 * Bump this once for a release. main.c prints it for -v / --version; the
 * Makefile reads it into VERSION (see `make version`) so release tags and web
 * build metadata can use the same string without a second copy to drift.
 */
#ifndef VERSION_H
#define VERSION_H

/* Includes the 'v' prefix used by release tags (e.g. v1.0.0). */
#define GPX2FIT_VERSION "v1.0.0"

#endif /* VERSION_H */
