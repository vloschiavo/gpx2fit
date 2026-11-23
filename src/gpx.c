/*
 * gpx.c: GPX reader built on expat.
 *
 * Namespace handling: the parser runs in expat's namespace mode, so element
 * names arrive as "uri local" (or just "local" with no namespace). Only
 * elements in no namespace or a topografix.com GPX namespace are matched;
 * anything else (Garmin TrackPointExtension, ...) is skipped but still
 * counted for nesting. This makes GPX 1.0, GPX 1.1, prefixed (gpx:trkpt) and
 * namespace-less files all work.
 *
 * Structure matched (depth 1 is the <gpx> root):
 *   gpx/wpt[@lat,@lon]/name
 *   gpx/trk/name
 *   gpx/trk/trkseg/trkpt[@lat,@lon]/ele
 * Routes (<rte>) and everything else are ignored.
 */
#define _POSIX_C_SOURCE 200809L

#include "gpx.h"

#include <ctype.h>
#include <errno.h>
#include <expat.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_MAX (64 * 1024) /* longest element text we keep, bytes */
#define GPX_NS_PREFIX "http://www.topografix.com/GPX/"

/* ---- name helpers ------------------------------------------------------ */

const char *gpx_local_name(const char *qname)
{
    const char *last = qname;
    for (const char *p = qname; *p; p++)
        if (*p == ' ' || *p == '}' || *p == ':')
            last = p + 1;
    return last;
}

bool gpx_ns_ok(const char *qname)
{
    if (strchr(qname, ' ') == NULL)
        return true; /* no namespace (or a bare prefix:name) */
    return strncmp(qname, GPX_NS_PREFIX, sizeof GPX_NS_PREFIX - 1) == 0;
}

/* ---- small utilities --------------------------------------------------- */

/* Strict decimal parse: optional surrounding whitespace, digits/sign/point/
 * exponent only (no hex, inf or nan), finite. 0 on success, -1 on error. */
static int parse_decimal(const char *s, double *out)
{
    while (isspace((unsigned char)*s))
        s++;
    if (*s == '\0')
        return -1;
    for (const char *p = s; *p; p++)
        if (!strchr("0123456789+-.eE", *p) && !isspace((unsigned char)*p))
            return -1;

    char *end;
    double v = strtod(s, &end);
    if (end == s)
        return -1;
    while (isspace((unsigned char)*end))
        end++;
    if (*end != '\0' || !isfinite(v))
        return -1;
    *out = v;
    return 0;
}

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

/* File name without directory and extension, e.g. "a/b/route.gpx" -> "route".
 * A NULL input yields "course". */
static char *name_from_path(const char *path)
{
    if (!path)
        path = "course";
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t n = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot && dot != base)
        n = (size_t)(dot - base);
    if (n == 0)
        return dup_str("course");
    char *d = malloc(n + 1);
    if (d) {
        memcpy(d, base, n);
        d[n] = '\0';
    }
    return d;
}

/* Great-circle distance in meters. Local copy until geo.c (task 3.1) exists;
 * only used to pick the longest track. */
static double hav_m(double lat1, double lon1, double lat2, double lon2)
{
    const double pi = 3.14159265358979323846;
    const double r = 6371008.8, k = pi / 180.0;
    double dlat = (lat2 - lat1) * k, dlon = (lon2 - lon1) * k;
    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1 * k) * cos(lat2 * k) * sin(dlon / 2) * sin(dlon / 2);
    return 2.0 * r * atan2(sqrt(a), sqrt(1.0 - a));
}

/* ---- parser state ------------------------------------------------------ */

struct gpx_trk {
    char         *name;  /* <trk><name>, NULL if absent */
    struct course pts;   /* trackpoints of all segments, used as an array only */
    size_t        n_seg; /* number of <trkseg> */
};

enum cap_kind { CAP_NONE, CAP_ELE, CAP_WPT_NAME, CAP_TRK_NAME };

struct gpx_state {
    XML_Parser                 parser;
    struct course             *c;
    const struct gpx_options  *opt;

    bool  failed;
    char  err[256];

    int   depth;                      /* current element depth, root = 1 */
    int   trk_depth, seg_depth, pt_depth, wpt_depth; /* 0 = not inside */

    struct gpx_trk *trks;
    size_t          n_trks, cap_trks;

    struct trackpoint tp;             /* trackpoint being read */
    double            wlat, wlon;     /* waypoint being read */
    char             *wname;

    enum cap_kind cap;                /* text currently being captured */
    int           cap_depth;
    char         *text;
    size_t        text_len, text_cap;
};

static void fail(struct gpx_state *st, const char *fmt, ...)
{
    if (st->failed)
        return;
    st->failed = true;
    int n = snprintf(st->err, sizeof st->err, "line %lu: ",
                     (unsigned long)XML_GetCurrentLineNumber(st->parser));
    if (n > 0 && (size_t)n < sizeof st->err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(st->err + n, sizeof st->err - (size_t)n, fmt, ap);
        va_end(ap);
    }
    XML_StopParser(st->parser, XML_FALSE);
}

static void warn(const struct gpx_state *st, const char *fmt, ...)
{
    if (st->opt->quiet)
        return;
    va_list ap;
    fputs("gpx2fit: warning: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static struct gpx_trk *cur_trk(struct gpx_state *st)
{
    return &st->trks[st->n_trks - 1];
}

static int trk_new(struct gpx_state *st)
{
    if (st->n_trks == st->cap_trks) {
        size_t n = st->cap_trks ? st->cap_trks * 2 : 4;
        struct gpx_trk *p = realloc(st->trks, n * sizeof *p);
        if (!p)
            return -1;
        st->trks = p;
        st->cap_trks = n;
    }
    struct gpx_trk *t = &st->trks[st->n_trks++];
    t->name = NULL;
    t->n_seg = 0;
    course_init(&t->pts);
    return 0;
}

/* ---- text capture ------------------------------------------------------ */

static void begin_capture(struct gpx_state *st, enum cap_kind k)
{
    st->cap = k;
    st->cap_depth = st->depth;
    st->text_len = 0;
}

static void on_text(void *ud, const XML_Char *s, int len)
{
    struct gpx_state *st = ud;
    if (st->failed || st->cap == CAP_NONE || st->depth != st->cap_depth)
        return;
    if (st->text_len + (size_t)len > TEXT_MAX)
        return; /* absurdly long: drop the excess */
    if (st->text_len + (size_t)len + 1 > st->text_cap) {
        size_t n = st->text_cap ? st->text_cap * 2 : 128;
        while (n < st->text_len + (size_t)len + 1)
            n *= 2;
        char *p = realloc(st->text, n);
        if (!p) {
            fail(st, "out of memory");
            return;
        }
        st->text = p;
        st->text_cap = n;
    }
    memcpy(st->text + st->text_len, s, (size_t)len);
    st->text_len += (size_t)len;
}

/* NUL-terminate and trim the captured text in place. */
static char *trimmed_text(struct gpx_state *st)
{
    static char empty[1] = "";
    if (!st->text)
        return empty;
    st->text[st->text_len] = '\0';
    char *s = st->text;
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        e--;
    *e = '\0';
    return s;
}

static void finish_capture(struct gpx_state *st)
{
    enum cap_kind k = st->cap;
    char *s = trimmed_text(st);
    st->cap = CAP_NONE;

    switch (k) {
    case CAP_ELE:
        if (*s == '\0') {
            st->tp.has_ele = false; /* empty <ele/> counts as absent */
        } else if (parse_decimal(s, &st->tp.ele) != 0) {
            fail(st, "invalid elevation '%.40s'", s);
        } else {
            st->tp.has_ele = true;
        }
        break;
    case CAP_WPT_NAME:
        if (*s != '\0') {
            free(st->wname);
            st->wname = dup_str(s);
            if (!st->wname)
                fail(st, "out of memory");
        }
        break;
    case CAP_TRK_NAME:
        if (*s != '\0') {
            struct gpx_trk *t = cur_trk(st);
            free(t->name);
            t->name = dup_str(s);
            if (!t->name)
                fail(st, "out of memory");
        }
        break;
    case CAP_NONE:
        break;
    }
}

/* ---- element handlers -------------------------------------------------- */

/* Read and validate the lat/lon attributes of <trkpt> or <wpt>. */
static int get_latlon(struct gpx_state *st, const XML_Char **atts,
                      const char *what, double *lat, double *lon)
{
    const char *slat = NULL, *slon = NULL;
    for (; atts[0]; atts += 2) {
        if (strcmp(atts[0], "lat") == 0)
            slat = atts[1];
        else if (strcmp(atts[0], "lon") == 0)
            slon = atts[1];
    }
    if (!slat || !slon) {
        fail(st, "<%s> is missing its %s attribute", what,
             slat ? "lon" : "lat");
        return -1;
    }
    if (parse_decimal(slat, lat) != 0 || *lat < -90.0 || *lat > 90.0) {
        fail(st, "invalid latitude '%.40s' in <%s> (need -90 to 90)", slat, what);
        return -1;
    }
    if (parse_decimal(slon, lon) != 0 || *lon < -180.0 || *lon > 180.0) {
        fail(st, "invalid longitude '%.40s' in <%s> (need -180 to 180)", slon,
             what);
        return -1;
    }
    return 0;
}

static void XMLCALL on_start(void *ud, const XML_Char *qname,
                             const XML_Char **atts)
{
    struct gpx_state *st = ud;
    if (st->failed)
        return;

    int d = ++st->depth;
    const char *ln = gpx_local_name(qname);

    if (d == 1) {
        if (!gpx_ns_ok(qname) || strcmp(ln, "gpx") != 0)
            fail(st, "not a GPX file (root element is <%s>)", ln);
        return;
    }
    if (!gpx_ns_ok(qname))
        return; /* foreign namespace: skip */

    if (d == 2 && strcmp(ln, "trk") == 0) {
        if (trk_new(st) != 0) {
            fail(st, "out of memory");
            return;
        }
        st->trk_depth = d;
    } else if (st->trk_depth && !st->seg_depth && d == st->trk_depth + 1 &&
               strcmp(ln, "trkseg") == 0) {
        st->seg_depth = d;
        cur_trk(st)->n_seg++;
    } else if (st->seg_depth && d == st->seg_depth + 1 &&
               strcmp(ln, "trkpt") == 0) {
        memset(&st->tp, 0, sizeof st->tp);
        if (get_latlon(st, atts, "trkpt", &st->tp.lat, &st->tp.lon) == 0)
            st->pt_depth = d;
    } else if (st->pt_depth && d == st->pt_depth + 1 &&
               strcmp(ln, "ele") == 0) {
        begin_capture(st, CAP_ELE);
    } else if (d == 2 && strcmp(ln, "wpt") == 0) {
        free(st->wname);
        st->wname = NULL;
        if (get_latlon(st, atts, "wpt", &st->wlat, &st->wlon) == 0)
            st->wpt_depth = d;
    } else if (st->wpt_depth && d == st->wpt_depth + 1 &&
               strcmp(ln, "name") == 0) {
        begin_capture(st, CAP_WPT_NAME);
    } else if (st->trk_depth && !st->seg_depth && d == st->trk_depth + 1 &&
               strcmp(ln, "name") == 0) {
        begin_capture(st, CAP_TRK_NAME);
    }
}

static void XMLCALL on_end(void *ud, const XML_Char *qname)
{
    struct gpx_state *st = ud;
    int d = st->depth--;
    (void)qname;
    if (st->failed)
        return;

    if (st->cap != CAP_NONE && d == st->cap_depth)
        finish_capture(st);
    if (st->failed)
        return;

    if (d == st->pt_depth) {
        if (course_add_trackpoint(&cur_trk(st)->pts, &st->tp) != 0)
            fail(st, "out of memory");
        st->pt_depth = 0;
    } else if (d == st->seg_depth) {
        st->seg_depth = 0;
    } else if (d == st->trk_depth) {
        st->trk_depth = 0;
    } else if (d == st->wpt_depth) {
        if (course_add_waypoint(st->c, st->wname, st->wlat, st->wlon) != 0)
            fail(st, "out of memory");
        free(st->wname);
        st->wname = NULL;
        st->wpt_depth = 0;
    }
}

/* ---- track selection --------------------------------------------------- */

static double trk_length_m(const struct gpx_trk *t)
{
    double len = 0.0;
    for (size_t i = 1; i < t->pts.n_tp; i++)
        len += hav_m(t->pts.tp[i - 1].lat, t->pts.tp[i - 1].lon,
                     t->pts.tp[i].lat, t->pts.tp[i].lon);
    return len;
}

/* Pick the track to use. Returns its index, or -1 with err filled in. */
static long select_track(const struct gpx_state *st, char *err, size_t errsz)
{
    long want = st->opt->track;

    if (want > 0) {
        if ((size_t)want > st->n_trks) {
            snprintf(err, errsz, "track %ld does not exist (file has %zu)", want,
                     st->n_trks);
            return -1;
        }
        if (st->trks[want - 1].pts.n_tp == 0) {
            snprintf(err, errsz, "track %ld has no trackpoints", want);
            return -1;
        }
        return want - 1;
    }

    long best = -1;
    size_t nonempty = 0;
    double best_len = -1.0;
    for (size_t i = 0; i < st->n_trks; i++) {
        if (st->trks[i].pts.n_tp == 0)
            continue;
        nonempty++;
        double len = trk_length_m(&st->trks[i]);
        if (len > best_len) { /* strict: ties keep the earlier track */
            best_len = len;
            best = (long)i;
        }
    }
    if (best < 0) {
        snprintf(err, errsz, "no trackpoints found");
        return -1;
    }
    if (nonempty > 1)
        warn(st, "file has %zu tracks; using track %ld, the longest "
             "(%.1f km). Use --track N to choose another.",
             nonempty, best + 1, best_len / 1000.0);
    return best;
}

/* ---- public entry point ------------------------------------------------ */

static void state_free(struct gpx_state *st)
{
    for (size_t i = 0; i < st->n_trks; i++) {
        free(st->trks[i].name);
        course_free(&st->trks[i].pts);
    }
    free(st->trks);
    free(st->wname);
    free(st->text);
}

int read_gpx_mem(const char *buf, size_t len, const char *fallback_name,
                 struct course *c, const struct gpx_options *opt,
                 char *err, size_t errsz)
{
    struct gpx_state st;
    int rc = -1;
    XML_Parser p;
    struct gpx_options default_opt;
    const char *data = buf ? buf : "";

    if (err && errsz > 0)
        err[0] = '\0';
    if (len > 0 && !buf) {
        if (err && errsz > 0)
            snprintf(err, errsz, "invalid input: NULL buffer");
        return -1;
    }

    memset(&st, 0, sizeof st);
    st.c = c;
    if (opt)
        st.opt = opt;
    else {
        default_opt.track = 0;
        default_opt.quiet = false;
        st.opt = &default_opt;
    }

    p = XML_ParserCreateNS(NULL, ' ');
    if (!p) {
        if (err && errsz > 0)
            snprintf(err, errsz, "out of memory");
        return -1;
    }
    st.parser = p;
    XML_SetUserData(p, &st);
    XML_SetElementHandler(p, on_start, on_end);
    XML_SetCharacterDataHandler(p, on_text);

    {
        size_t pos = 0;
        const size_t CHUNK = 64 * 1024;
        for (;;) {
            size_t n = len - pos;
            if (n > CHUNK)
                n = CHUNK;
            int final = (pos + n == len);
            if (XML_Parse(p, data + pos, (int)n, final) == XML_STATUS_ERROR) {
                if (st.failed) {
                    if (err && errsz > 0)
                        snprintf(err, errsz, "%s", st.err);
                } else {
                    if (err && errsz > 0)
                        snprintf(err, errsz, "line %lu: %s",
                                 (unsigned long)XML_GetCurrentLineNumber(p),
                                 XML_ErrorString(XML_GetErrorCode(p)));
                }
                goto done;
            }
            pos += n;
            if (final)
                break;
        }
    }

    {
        char ebuf[256];
        char *euse = (err && errsz > 0) ? err : ebuf;
        size_t esz = (err && errsz > 0) ? errsz : sizeof ebuf;
        if (!err || errsz == 0)
            ebuf[0] = '\0';

        long sel = select_track(&st, euse, esz);
        if (sel < 0) {
            if (err && errsz > 0 && st.err[0] && !err[0])
                snprintf(err, errsz, "%s", st.err);
            goto done;
        }
        struct gpx_trk *t = &st.trks[sel];

        if (t->n_seg > 1)
            warn(&st, "track %ld has %zu segments; joining them in order",
                 sel + 1, t->n_seg);

        /* Take over the track's point array. */
        free(c->tp);
        c->tp = t->pts.tp;
        c->n_tp = t->pts.n_tp;
        c->cap_tp = t->pts.cap_tp;
        t->pts.tp = NULL;
        t->pts.n_tp = t->pts.cap_tp = 0;

        free(c->name);
        if (t->name) {
            c->name = t->name;
            t->name = NULL;
        } else {
            c->name = name_from_path(fallback_name);
        }
        if (!c->name) {
            if (err && errsz > 0)
                snprintf(err, errsz, "out of memory");
            goto done;
        }
    }
    rc = 0;

done:
    XML_ParserFree(p);
    state_free(&st);
    return rc;
}

int read_gpx(const char *path, struct course *c, const struct gpx_options *opt,
             char *err, size_t errsz)
{
    FILE *f;
    char *buf = NULL;
    size_t len = 0, cap = 0;
    int rc;

    if (err && errsz > 0)
        err[0] = '\0';

    f = fopen(path, "rb");
    if (!f) {
        if (err && errsz > 0)
            snprintf(err, errsz, "cannot open '%s': %s", path, strerror(errno));
        return -1;
    }

    for (;;) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : 64 * 1024;
            char *nb = realloc(buf, ncap);
            if (!nb) {
                free(buf);
                fclose(f);
                if (err && errsz > 0)
                    snprintf(err, errsz, "out of memory");
                return -1;
            }
            buf = nb;
            cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        if (got == 0) {
            if (ferror(f)) {
                snprintf(err, errsz, "error reading '%s'", path);
                free(buf);
                fclose(f);
                return -1;
            }
            break;
        }
        len += got;
    }
    fclose(f);

    rc = read_gpx_mem(buf, len, path, c, opt, err, errsz);
    free(buf);
    return rc;
}
