/* symmap.c: name -> FIT course_point type rules (tasks 5.2 to 5.4). */
#include "symmap.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "default_map.h" /* generated from data/default.map: default_map_text */
#include "names.h"

/* Largest map file we will read, bytes. */
#define MAP_FILE_MAX (1024 * 1024)

/* ---- FIT type names (Profile 21.222, enum course_point) ------------------ */

static const struct { const char *name; uint8_t value; } type_table[] = {
    { "generic", 0 },        { "summit", 1 },        { "valley", 2 },
    { "water", 3 },          { "food", 4 },          { "danger", 5 },
    { "left", 6 },           { "right", 7 },         { "straight", 8 },
    { "first_aid", 9 },      { "fourth_category", 10 },
    { "third_category", 11 },{ "second_category", 12 },
    { "first_category", 13 },{ "hors_category", 14 },{ "sprint", 15 },
    { "left_fork", 16 },     { "right_fork", 17 },   { "middle_fork", 18 },
    { "slight_left", 19 },   { "sharp_left", 20 },   { "slight_right", 21 },
    { "sharp_right", 22 },   { "u_turn", 23 },       { "segment_start", 24 },
    { "segment_end", 25 },   { "campsite", 27 },     { "aid_station", 28 },
    { "rest_area", 29 },     { "general_distance", 30 },
    { "service", 31 },       { "energy_gel", 32 },   { "sports_drink", 33 },
    { "mile_marker", 34 },   { "checkpoint", 35 },   { "shelter", 36 },
    { "meeting_spot", 37 },  { "overlook", 38 },     { "toilet", 39 },
    { "shower", 40 },        { "gear", 41 },         { "sharp_curve", 42 },
    { "steep_incline", 43 }, { "tunnel", 44 },       { "bridge", 45 },
    { "obstacle", 46 },      { "crossing", 47 },     { "store", 48 },
    /* The FIT Profile has no distinct "shopping" course_point value; use the
     * store icon (48) so "market = shopping" rules encode cleanly. */
    { "shopping", 48 },
    { "transition", 49 },    { "navaid", 50 },       { "transport", 51 },
    { "alert", 52 },         { "info", 53 }
};

/* ---- small helpers ------------------------------------------------------- */

/* ASCII-only lower-casing; bytes >= 0x80 (UTF-8) are left alone, and the
 * result does not depend on the locale. */
static int lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Does the NUL-terminated needle occur in hay, ignoring ASCII case? */
static bool contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);

    if (nl == 0)
        return true;
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < nl && hay[i] &&
               lower((unsigned char)hay[i]) == lower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return true;
    }
    return false;
}

/* Equal ignoring ASCII case: the (not NUL-terminated) bytes a[0..alen) and the
 * NUL-terminated string b. */
static bool equal_ci_n(const char *a, size_t alen, const char *b)
{
    if (strlen(b) != alen)
        return false;
    for (size_t i = 0; i < alen; i++)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return false;
    return true;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Narrow [*s, *s + *len) to exclude leading and trailing blanks. */
static void trim(const char **s, size_t *len)
{
    while (*len > 0 && is_blank((*s)[0])) {
        (*s)++;
        (*len)--;
    }
    while (*len > 0 && is_blank((*s)[*len - 1]))
        (*len)--;
}

static void warnf(symmap_warn_fn warn, void *ctx, const char *source, int line,
                  const char *fmt, ...)
{
    char buf[160];
    va_list ap;

    if (!warn)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    warn(source, line, buf, ctx);
}

/* ---- type names ---------------------------------------------------------- */

static int type_from_bytes(const char *s, size_t len, uint8_t *type)
{
    for (size_t i = 0; i < sizeof type_table / sizeof *type_table; i++) {
        if (equal_ci_n(s, len, type_table[i].name)) {
            *type = type_table[i].value;
            return 0;
        }
    }
    return -1;
}

int symmap_type_from_name(const char *name, uint8_t *type)
{
    return name ? type_from_bytes(name, strlen(name), type) : -1;
}

/* ---- the rule list ------------------------------------------------------- */

void symmap_init(struct symmap *m)
{
    m->rules = NULL;
    m->n = 0;
    m->cap = 0;
}

void symmap_free(struct symmap *m)
{
    for (size_t i = 0; i < m->n; i++)
        free(m->rules[i].keyword);
    free(m->rules);
    symmap_init(m);
}

/* Append a rule; the keyword is copied from len bytes. */
static int add_rule(struct symmap *m, const char *kw, size_t len, uint8_t type)
{
    char *copy;

    if (m->n == m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 16;
        struct symmap_rule *nr = realloc(m->rules, ncap * sizeof *nr);

        if (!nr)
            return SYMMAP_NOMEM;
        m->rules = nr;
        m->cap = ncap;
    }
    copy = malloc(len + 1);
    if (!copy)
        return SYMMAP_NOMEM;
    memcpy(copy, kw, len);
    copy[len] = '\0';
    m->rules[m->n].keyword = copy;
    m->rules[m->n].type = type;
    m->n++;
    return SYMMAP_OK;
}

/* Handle one line (not NUL-terminated, without its '\n'). */
static int parse_line(const char *p, size_t len, int line, const char *source,
                      struct symmap *m, symmap_warn_fn warn, void *ctx)
{
    const char *hash = memchr(p, '#', len);
    const char *eq, *kw, *ty;
    size_t kwlen, tylen;
    uint8_t type;

    if (hash)
        len = (size_t)(hash - p);
    trim(&p, &len);
    if (len == 0)
        return SYMMAP_OK; /* blank or comment-only */

    eq = memchr(p, '=', len);
    if (!eq) {
        warnf(warn, ctx, source, line,
              "malformed line, expected 'keyword = type'");
        return SYMMAP_OK;
    }
    kw = p;
    kwlen = (size_t)(eq - p);
    ty = eq + 1;
    tylen = len - kwlen - 1;
    trim(&kw, &kwlen);
    trim(&ty, &tylen);
    if (kwlen == 0 || tylen == 0) {
        warnf(warn, ctx, source, line,
              "malformed line, expected 'keyword = type'");
        return SYMMAP_OK;
    }
    if (type_from_bytes(ty, tylen, &type) != 0) {
        warnf(warn, ctx, source, line, "unknown type '%.*s'",
              (int)(tylen > 40 ? 40 : tylen), ty);
        return SYMMAP_OK;
    }
    return add_rule(m, kw, kwlen, type);
}

int symmap_parse(const char *text, const char *source, struct symmap *m,
                 symmap_warn_fn warn, void *ctx)
{
    const char *p = text;
    int line = 0;

    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        int rc;

        line++;
        rc = parse_line(p, len, line, source, m, warn, ctx);
        if (rc != SYMMAP_OK)
            return rc;
        p += len;
        if (*p == '\n')
            p++;
    }
    return SYMMAP_OK;
}

bool symmap_find(const struct symmap *m, const char *name, uint8_t *type)
{
    *type = FIT_CP_GENERIC;
    if (!name)
        return false;
    for (size_t i = 0; i < m->n; i++) {
        if (contains_ci(name, m->rules[i].keyword)) {
            *type = m->rules[i].type;
            return true;
        }
    }
    return false;
}

uint8_t symmap_lookup(const struct symmap *m, const char *name)
{
    uint8_t t;

    symmap_find(m, name, &t);
    return t;
}

/* ---- defaults and --map layering ---------------------------------------- */

int symmap_load_defaults(struct symmap *m, symmap_warn_fn warn, void *ctx)
{
    return symmap_parse(default_map_text, "default.map", m, warn, ctx);
}

/* Read a whole file into a new NUL-terminated buffer (*out, caller frees).
 * Returns SYMMAP_OK, or SYMMAP_IOERR / SYMMAP_NOMEM with a message in err. */
static int read_file(const char *path, char **out, char *err, size_t err_size)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL, *nb;
    size_t len = 0, cap = 0, got;

    if (!f) {
        snprintf(err, err_size, "cannot read map file '%s': %s", path,
                 strerror(errno));
        return SYMMAP_IOERR;
    }
    for (;;) {
        if (cap - len < 4096) {
            cap = cap ? cap * 2 : 8192;
            nb = realloc(buf, cap);
            if (!nb) {
                snprintf(err, err_size, "out of memory");
                free(buf);
                fclose(f);
                return SYMMAP_NOMEM;
            }
            buf = nb;
        }
        got = fread(buf + len, 1, cap - len - 1, f);
        len += got;
        if (len > MAP_FILE_MAX) {
            snprintf(err, err_size, "map file '%s' is too large (over %d bytes)",
                     path, MAP_FILE_MAX);
            free(buf);
            fclose(f);
            return SYMMAP_IOERR;
        }
        if (got == 0)
            break;
    }
    if (ferror(f)) {
        snprintf(err, err_size, "cannot read map file '%s': %s", path,
                 strerror(errno));
        free(buf);
        fclose(f);
        return SYMMAP_IOERR;
    }
    fclose(f);
    buf[len] = '\0';
    if (strlen(buf) != len) {
        snprintf(err, err_size, "map file '%s' contains a NUL byte", path);
        free(buf);
        return SYMMAP_IOERR;
    }
    *out = buf;
    return SYMMAP_OK;
}

int symmap_load(struct symmap *m, const char *user_file, symmap_warn_fn warn,
                void *ctx, char *err, size_t err_size)
{
    int rc;

    if (err_size > 0)
        err[0] = '\0';
    if (user_file) {
        char *text;

        rc = read_file(user_file, &text, err, err_size);
        if (rc != SYMMAP_OK)
            return rc;
        rc = symmap_parse(text, user_file, m, warn, ctx);
        free(text);
        if (rc != SYMMAP_OK) {
            snprintf(err, err_size, "out of memory");
            return rc;
        }
    }
    rc = symmap_load_defaults(m, warn, ctx);
    if (rc != SYMMAP_OK)
        snprintf(err, err_size, "out of memory");
    return rc;
}

/* ---- applying to a course ------------------------------------------------ */

size_t course_apply_types(struct course *c, const struct symmap *m, bool quiet,
                          struct diag_list *diag)
{
    size_t unmatched = 0;

    for (size_t i = 0; i < c->n_cp; i++) {
        struct course_pt *p = &c->cp[i];
        uint8_t type = FIT_CP_GENERIC;

        if (p->name && p->name[0]) {
            size_t len = strlen(p->name);

            if (!symmap_find(m, p->name, &type)) {
                unmatched++;
                if (diag)
                    diag_list_add(diag,
                                  "warning: no type for '%s', using generic; "
                                  "add a line to --map\n", p->name);
                if (!quiet)
                    fprintf(stderr, "warning: no type for '%s', using generic; "
                            "add a line to --map\n", p->name);
            }
            if (len > COURSE_POINT_NAME_MAX) {
                if (diag)
                    diag_list_add(diag,
                                  "warning: course point name '%s' is longer "
                                  "than %d bytes, truncating\n", p->name,
                                  COURSE_POINT_NAME_MAX);
                if (!quiet)
                    fprintf(stderr, "warning: course point name '%s' is longer "
                            "than %d bytes, truncating\n", p->name,
                            COURSE_POINT_NAME_MAX);
                utf8_truncate(p->name, len + 1, p->name, COURSE_POINT_NAME_MAX);
            }
        }
        p->type = type;
    }
    return unmatched;
}
