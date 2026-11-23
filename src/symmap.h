/*
 * symmap.h: waypoint name -> FIT course_point type (tasks 5.2 to 5.4).
 *
 * CalTopo exports no <sym>, so the type comes from the waypoint name through
 * an ordered list of rules. Rule file format, one rule per line:
 *
 *     keyword = type        # comment
 *
 *   - '#' starts a comment (to the end of the line); blank lines are ignored.
 *   - keyword: any text without '=' or '#'; leading/trailing blanks trimmed;
 *     inner spaces are kept ("first aid").
 *   - type: a FIT course_point type name such as water, danger, campsite,
 *     generic (see symmap.c for the full table), any letter case.
 *   - Matching is a case-insensitive (ASCII) SUBSTRING match: the keyword may
 *     appear anywhere in the name. The first matching rule in order wins, so
 *     put specific keywords above general ones.
 */
#ifndef SYMMAP_H
#define SYMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "course.h"
#include "diag.h"

/* FIT course_point type enum (Profile 21.222, confirmed in NOTES.md 5.1).
 * The parser accepts every name in the profile (see symmap.c); these are the
 * ones the code itself refers to. */
enum fit_cp_type {
    FIT_CP_GENERIC      = 0,
    FIT_CP_SUMMIT       = 1,
    FIT_CP_VALLEY       = 2,
    FIT_CP_WATER        = 3,
    FIT_CP_FOOD         = 4,
    FIT_CP_DANGER       = 5,
    FIT_CP_FIRST_AID    = 9,
    FIT_CP_CAMPSITE     = 27,
    FIT_CP_AID_STATION  = 28,
    FIT_CP_REST_AREA    = 29,
    FIT_CP_CHECKPOINT   = 35,
    FIT_CP_SHELTER      = 36,
    FIT_CP_MEETING_SPOT = 37,
    FIT_CP_OVERLOOK     = 38,
    FIT_CP_TOILET       = 39,
    FIT_CP_SHOWER       = 40
};

/* Return codes. */
#define SYMMAP_OK     0
#define SYMMAP_IOERR (-1) /* file could not be read; message in err */
#define SYMMAP_NOMEM (-2)

struct symmap_rule {
    char   *keyword; /* heap-allocated, as written in the file */
    uint8_t type;    /* FIT course_point type value */
};

struct symmap {
    struct symmap_rule *rules; /* in match order */
    size_t              n;     /* rules in use */
    size_t              cap;   /* rules allocated */
};

/* Called for each bad line. source is the file name (or "default.map"), line
 * is 1-based, msg is a short description. ctx is passed through untouched. */
typedef void (*symmap_warn_fn)(const char *source, int line, const char *msg,
                               void *ctx);

void symmap_init(struct symmap *m);
void symmap_free(struct symmap *m); /* leaves m empty and reusable */

/* Parse rule text (NUL-terminated) and APPEND its rules to m, after any
 * already there. Bad lines (no '=', empty keyword or type, unknown type name)
 * are skipped with a warn call (warn may be NULL). source is only used for
 * warnings. Returns SYMMAP_OK or SYMMAP_NOMEM. */
int symmap_parse(const char *text, const char *source, struct symmap *m,
                 symmap_warn_fn warn, void *ctx);

/* Find the first rule whose keyword occurs in name. Returns true and sets *type
 * on a match; false (and *type = generic) if nothing matches or name is NULL.
 * A rule that maps to generic still counts as a match. */
bool symmap_find(const struct symmap *m, const char *name, uint8_t *type);

/* symmap_find without the flag: no match returns FIT_CP_GENERIC. */
uint8_t symmap_lookup(const struct symmap *m, const char *name);

/* Resolve a FIT type name (case-insensitive). Returns 0 and sets *type, or -1
 * if the name is unknown. */
int symmap_type_from_name(const char *name, uint8_t *type);

/* Append the rules embedded from data/default.map. Returns SYMMAP_OK or
 * SYMMAP_NOMEM. */
int symmap_load_defaults(struct symmap *m, symmap_warn_fn warn, void *ctx);

/* The rule set the program uses: the rules from user_file (if not NULL) first,
 * then the embedded defaults, so user rules win and unmatched names fall
 * through to the defaults. On SYMMAP_IOERR / SYMMAP_NOMEM a message is written
 * to err (err_size bytes). */
int symmap_load(struct symmap *m, const char *user_file, symmap_warn_fn warn,
                void *ctx, char *err, size_t err_size);

/* Set cp[i].type for every course point from its name, then truncate names to
 * COURSE_POINT_NAME_MAX bytes (after matching, so the full name is matched).
 * Unless quiet, prints to stderr a warning for each named point that matches
 * no rule and for each truncated name. Unnamed points get generic silently.
 * Returns the number of named points that matched no rule. */
size_t course_apply_types(struct course *c, const struct symmap *m, bool quiet,
                         struct diag_list *diag);

#endif /* SYMMAP_H */
