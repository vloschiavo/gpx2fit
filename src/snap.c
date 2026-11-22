/*
 * snap.c: snap waypoints onto the track (tasks 4.1 to 4.6). See snap.h.
 *
 * Pass modes and tie-breaking (all deterministic):
 *
 *   A "candidate" is one local minimum of the waypoint-to-track distance, one
 *   per pass of the track. Candidates are in track order.
 *
 *   nearest  The candidate with the smallest offset. Offsets closer than
 *            OFFSET_EPS (1 micrometer) count as equal, and an equal offset
 *            goes to the EARLIER candidate (smaller distance along course).
 *   first    The earliest candidate.
 *   last     The latest candidate.
 *   ordered  Waypoints are taken in GPX order. We pick one candidate per
 *            waypoint so that the distance along the course never decreases
 *            (equal is allowed: duplicate waypoints). Among all such chains:
 *              1. keep as many waypoints in the chain as possible;
 *              2. then minimize the total offset;
 *              3. then prefer the chain whose earliest differing node comes
 *                 first (lowest waypoint index, then lowest candidate index).
 *            Waypoints that cannot join the longest chain (the GPX order
 *            contradicts the track order) fall back to their nearest
 *            candidate and are counted in n_unordered.
 *   Waypoints with no candidate within the threshold are skipped in every
 *   mode and take no part in the ordering.
 */
#include "snap.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_ 3.14159265358979323846
#define DEG2RAD (PI_ / 180.0)

#define T_EPS 1e-9       /* t within this of 0 or 1 counts as clamped */
#define OFFSET_EPS 1e-6  /* meters: offsets closer than this are equal */
#define BOX_SLACK_DEG 1e-9 /* keeps the prefilter conservative vs rounding */

/* ---- helpers ------------------------------------------------------------ */

static double wrap360(double d)
{
    while (d > 180.0)
        d -= 360.0;
    while (d < -180.0)
        d += 360.0;
    return d;
}

static bool seg_is_zero(const struct course *c, size_t i)
{
    return c->tp[i].lat == c->tp[i + 1].lat &&
           c->tp[i].lon == c->tp[i + 1].lon;
}

/* The next segment after i that has nonzero length (n_seg if none). */
static size_t next_eff(const struct course *c, size_t i, size_t n_seg)
{
    size_t j = i + 1;
    while (j < n_seg && seg_is_zero(c, j))
        j++;
    return j;
}

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static bool offset_less(double a, double b)
{
    return a < b - OFFSET_EPS;
}

int snap_mode_parse(const char *s, enum snap_mode *out)
{
    static const struct { const char *name; enum snap_mode m; } tab[] = {
        { "nearest", SNAP_NEAREST }, { "ordered", SNAP_ORDERED },
        { "first",   SNAP_FIRST   }, { "last",    SNAP_LAST    },
    };
    for (size_t i = 0; i < sizeof tab / sizeof *tab; i++) {
        if (strcmp(s, tab[i].name) == 0) {
            *out = tab[i].m;
            return 0;
        }
    }
    return -1;
}

const char *snap_mode_name(enum snap_mode m)
{
    switch (m) {
    case SNAP_NEAREST: return "nearest";
    case SNAP_ORDERED: return "ordered";
    case SNAP_FIRST:   return "first";
    case SNAP_LAST:    return "last";
    }
    return "nearest";
}

/* ---- 4.1 Bounding-box prefilter ---------------------------------------- */

int snap_index_init(struct snap_index *ix, const struct course *c)
{
    ix->c = c;
    ix->n_seg = c->n_tp >= 2 ? c->n_tp - 1 : 0;
    ix->box = NULL;
    if (ix->n_seg == 0)
        return 0;

    ix->box = malloc(ix->n_seg * sizeof *ix->box);
    if (!ix->box) {
        ix->n_seg = 0;
        return -1;
    }
    for (size_t i = 0; i < ix->n_seg; i++) {
        const struct trackpoint *a = &c->tp[i], *b = &c->tp[i + 1];
        double blon = a->lon + wrap360(b->lon - a->lon); /* unwrapped */
        double lo = fmin(a->lon, blon), hi = fmax(a->lon, blon);
        struct seg_box *bx = &ix->box[i];

        bx->lat_lo = fmin(a->lat, b->lat);
        bx->lat_hi = fmax(a->lat, b->lat);
        bx->lon_mid = (lo + hi) / 2.0;
        bx->lon_half = (hi - lo) / 2.0;
    }
    return 0;
}

void snap_index_free(struct snap_index *ix)
{
    free(ix->box);
    ix->box = NULL;
    ix->n_seg = 0;
}

struct query_box {
    double lat, lon, dlat, dlon;
};

static bool box_hit(const struct seg_box *bx, const struct query_box *q)
{
    if (q->lat < bx->lat_lo - q->dlat || q->lat > bx->lat_hi + q->dlat)
        return false;
    return fabs(wrap360(q->lon - bx->lon_mid)) <= bx->lon_half + q->dlon;
}

int snap_prefilter(const struct snap_index *ix, double lat, double lon,
                   double thr_m, size_t **segs, size_t *n)
{
    struct query_box q;
    double m_lat = EARTH_RADIUS_M * DEG2RAD;
    double m_lon = m_lat * cos(lat * DEG2RAD);
    size_t count = 0;

    *segs = NULL;
    *n = 0;
    if (ix->n_seg == 0)
        return 0;
    if (thr_m < 0.0)
        thr_m = 0.0;

    /* Same local projection as project_to_segment() (centered on the
     * query point), so a box test that fails really implies dist > thr. */
    q.lat = lat;
    q.lon = lon;
    q.dlat = thr_m / m_lat + BOX_SLACK_DEG;
    q.dlon = (m_lon < 1e-3) ? 360.0 : thr_m / m_lon + BOX_SLACK_DEG;

    for (size_t i = 0; i < ix->n_seg; i++)
        count += box_hit(&ix->box[i], &q);
    if (count == 0)
        return 0;

    size_t *out = malloc(count * sizeof *out);
    if (!out)
        return -1;
    size_t k = 0;
    for (size_t i = 0; i < ix->n_seg; i++)
        if (box_hit(&ix->box[i], &q))
            out[k++] = i;
    *segs = out;
    *n = count;
    return 0;
}

/* ---- 4.2 Candidate snaps ----------------------------------------------- */

enum { CLS_LEFT, CLS_INTERIOR, CLS_RIGHT };

/* A segment whose projection of the waypoint lies within the threshold. */
struct hit {
    size_t          i;
    struct seg_proj p;
    int             cls; /* where on the segment the closest point is */
};

static double along(const struct course *c, size_t i, double t)
{
    return c->tp[i].dist + t * (c->tp[i + 1].dist - c->tp[i].dist);
}

static int cands_push(struct snap_cands *cs, const struct course *c,
                      const struct hit *h)
{
    if (cs->n == cs->cap) {
        size_t cap = cs->cap ? cs->cap * 2 : 4;
        void *p = realloc(cs->v, cap * sizeof *cs->v);
        if (!p)
            return -1;
        cs->v = p;
        cs->cap = cap;
    }
    cs->v[cs->n++] = (struct snap_candidate){
        .seg = h->i, .t = h->p.t, .dist = along(c, h->i, h->p.t),
        .offset = h->p.dist_m, .lat = h->p.lat, .lon = h->p.lon,
    };
    return 0;
}

static double vertex_offset(const struct local_proj *pr,
                            const struct trackpoint *tp)
{
    double x, y;
    proj_forward(pr, tp->lat, tp->lon, &x, &y);
    return hypot(x, y);
}

/* Are candidates a and b (a earlier) on the same pass? They are unless the
 * track rises SNAP_PASS_SEP_M above the shallower of the two in between.
 * The distance to a polyline is convex on each segment, so its maximum
 * between a and b is at a vertex. */
static bool same_pass(const struct course *c, const struct local_proj *pr,
                      const struct snap_candidate *a,
                      const struct snap_candidate *b)
{
    double limit = fmax(a->offset, b->offset) + SNAP_PASS_SEP_M;

    for (size_t v = a->seg + 1; v <= b->seg; v++)
        if (vertex_offset(pr, &c->tp[v]) >= limit)
            return false;
    return true;
}

int snap_candidates(const struct snap_index *ix, double lat, double lon,
                    double thr_m, struct snap_cands *out)
{
    const struct course *c = ix->c;
    size_t *segs = NULL, n_segs = 0, nh = 0;
    struct hit *hits = NULL;
    int rc = -1;

    out->v = NULL;
    out->n = out->cap = 0;
    if (thr_m < 0.0)
        thr_m = 0.0;

    if (snap_prefilter(ix, lat, lon, thr_m, &segs, &n_segs) != 0)
        return -1;
    if (n_segs == 0)
        return 0;

    hits = malloc(n_segs * sizeof *hits);
    if (!hits)
        goto done;

    /* Exact projection for the survivors (zero-length segments ignored). */
    for (size_t k = 0; k < n_segs; k++) {
        size_t i = segs[k];
        if (seg_is_zero(c, i))
            continue;
        struct seg_proj p = project_to_segment(lat, lon,
            c->tp[i].lat, c->tp[i].lon, c->tp[i + 1].lat, c->tp[i + 1].lon);
        if (!(p.dist_m <= thr_m))
            continue;
        hits[nh].i = i;
        hits[nh].p = p;
        hits[nh].cls = p.t <= T_EPS ? CLS_LEFT
                     : p.t >= 1.0 - T_EPS ? CLS_RIGHT : CLS_INTERIOR;
        nh++;
    }

    /*
     * Local minima. Within one segment the distance is convex, so:
     *  - an interior projection is a local minimum;
     *  - at a shared vertex, the distance falls into it when the previous
     *    segment clamps at its end (RIGHT) and rises out of it when the
     *    next clamps at its start (LEFT): a minimum only if both hold;
     *  - the end of a run of hit segments (track end, or the neighbour is
     *    beyond the threshold) is a minimum if the distance is falling into
     *    it (RIGHT) or rising out of it (LEFT).
     */
    for (size_t k = 0; k < nh; k++) {
        const struct hit *h = &hits[k];
        bool prev_adj = k > 0 && next_eff(c, hits[k - 1].i, ix->n_seg) == h->i;
        bool next_adj = k + 1 < nh &&
                        next_eff(c, h->i, ix->n_seg) == hits[k + 1].i;
        const struct hit *emit = NULL;

        if (h->cls == CLS_INTERIOR)
            emit = h;
        else if (h->cls == CLS_LEFT) {
            if (!prev_adj)
                emit = h;
            else if (hits[k - 1].cls == CLS_RIGHT)
                emit = &hits[k - 1]; /* the shared vertex */
        } else if (!next_adj) {
            emit = h;
        }
        if (emit && cands_push(out, c, emit) != 0)
            goto done;
    }

    /* Merge jitter: minima on the same pass collapse to the best one. */
    if (out->n > 1) {
        struct local_proj pr;
        size_t m = 0;

        proj_init(&pr, lat, lon);
        for (size_t k = 0; k < out->n; k++) {
            struct snap_candidate cur = out->v[k];
            if (m > 0 && same_pass(c, &pr, &out->v[m - 1], &cur)) {
                if (offset_less(cur.offset, out->v[m - 1].offset))
                    out->v[m - 1] = cur;
            } else {
                out->v[m++] = cur;
            }
        }
        out->n = m;
    }
    rc = 0;

done:
    if (rc != 0)
        snap_cands_free(out);
    free(hits);
    free(segs);
    return rc;
}

void snap_cands_free(struct snap_cands *cs)
{
    free(cs->v);
    cs->v = NULL;
    cs->n = cs->cap = 0;
}

double snap_nearest_offset(const struct course *c, double lat, double lon)
{
    double best = -1.0;

    if (c->n_tp == 1)
        return haversine_m(lat, lon, c->tp[0].lat, c->tp[0].lon);
    for (size_t i = 0; i + 1 < c->n_tp; i++) {
        struct seg_proj p = project_to_segment(lat, lon,
            c->tp[i].lat, c->tp[i].lon, c->tp[i + 1].lat, c->tp[i + 1].lon);
        if (best < 0.0 || p.dist_m < best)
            best = p.dist_m;
    }
    return best;
}

/* ---- 4.3 to 4.5 Choosing a pass ---------------------------------------- */

static int pick_nearest(const struct snap_cands *cs)
{
    size_t best = 0;
    for (size_t j = 1; j < cs->n; j++)
        if (offset_less(cs->v[j].offset, cs->v[best].offset))
            best = j;
    return (int)best;
}

struct dp_node {
    size_t w, j;     /* waypoint and candidate index */
    size_t cnt;      /* waypoints in the best chain ending here */
    double cost;     /* total offset of that chain */
    long   prev;     /* previous node of that chain, or -1 */
    double dist, off;
};

/* Ordered mode. choice[w] must hold the nearest-pass fallback on entry
 * (-1 for waypoints without candidates); chain members are overwritten.
 * O(N^2) in the number of candidates N, which is small (waypoints x passes). */
static int choose_ordered(const struct snap_cands *all, size_t nw,
                          int *choice, bool *in_chain)
{
    size_t n = 0, u = 0;
    struct dp_node *nd;
    long best = -1;

    for (size_t w = 0; w < nw; w++)
        n += all[w].n;
    if (n == 0)
        return 0;
    nd = malloc(n * sizeof *nd);
    if (!nd)
        return -1;

    for (size_t w = 0; w < nw; w++)
        for (size_t j = 0; j < all[w].n; j++, u++)
            nd[u] = (struct dp_node){ w, j, 1, all[w].v[j].offset, -1,
                                      all[w].v[j].dist, all[w].v[j].offset };

    for (u = 0; u < n; u++) {
        /* Nodes are in waypoint order, so earlier waypoints come first. */
        for (size_t v = 0; v < u && nd[v].w < nd[u].w; v++) {
            if (nd[v].dist > nd[u].dist)
                continue;
            size_t cnt = nd[v].cnt + 1;
            double cost = nd[v].cost + nd[u].off;
            if (cnt > nd[u].cnt ||
                (cnt == nd[u].cnt && offset_less(cost, nd[u].cost))) {
                nd[u].cnt = cnt;
                nd[u].cost = cost;
                nd[u].prev = (long)v;
            }
        }
    }

    for (u = 0; u < n; u++)
        if (best < 0 || nd[u].cnt > nd[best].cnt ||
            (nd[u].cnt == nd[best].cnt &&
             offset_less(nd[u].cost, nd[best].cost)))
            best = (long)u;

    for (long x = best; x >= 0; x = nd[x].prev) {
        choice[nd[x].w] = (int)nd[x].j;
        in_chain[nd[x].w] = true;
    }
    free(nd);
    return 0;
}

/* ---- 4.6 Course points, sorting, summary ------------------------------- */

static const char *display_name(const char *name)
{
    return name ? name : "(unnamed)";
}

static void clear_course_points(struct course *c)
{
    for (size_t i = 0; i < c->n_cp; i++)
        free(c->cp[i].name);
    c->n_cp = 0;
}

static int add_course_point(struct course *c, const struct waypoint *w,
                            const struct snap_candidate *s)
{
    struct course_pt *p;

    if (c->n_cp == c->cap_cp) {
        size_t cap = c->cap_cp ? c->cap_cp * 2 : 16;
        void *q = realloc(c->cp, cap * sizeof *c->cp);
        if (!q)
            return -1;
        c->cp = q;
        c->cap_cp = cap;
    }
    char *name = dup_str(w->name ? w->name : "");
    if (!name)
        return -1;
    p = &c->cp[c->n_cp++];
    p->name = name;
    p->lat = s->lat;
    p->lon = s->lon;
    p->dist = s->dist;
    p->offset = s->offset;
    p->t = 0;
    p->type = 0;
    return 0;
}

void snap_sort_course_points(struct course *c)
{
    /* Stable insertion sort: n is the number of waypoints (hundreds at most). */
    for (size_t i = 1; i < c->n_cp; i++) {
        struct course_pt x = c->cp[i];
        size_t j = i;
        while (j > 0 && c->cp[j - 1].dist > x.dist) {
            c->cp[j] = c->cp[j - 1];
            j--;
        }
        c->cp[j] = x;
    }
}

int snap_waypoints(struct course *c, const struct snap_options *opt,
                   struct snap_report *rep)
{
    struct snap_report r = { 0, 0, 0 };
    struct snap_index ix;
    struct snap_cands *all = NULL;
    int *choice = NULL;
    bool *in_chain = NULL;
    size_t nw = c->n_wp;
    int rc = SNAP_NOMEM;

    clear_course_points(c);
    if (rep)
        *rep = r;
    if (nw == 0)
        return SNAP_OK;
    if (snap_index_init(&ix, c) != 0)
        return SNAP_NOMEM;

    all = calloc(nw, sizeof *all);
    choice = malloc(nw * sizeof *choice);
    in_chain = calloc(nw, sizeof *in_chain);
    if (!all || !choice || !in_chain)
        goto done;

    for (size_t w = 0; w < nw; w++) {
        if (snap_candidates(&ix, c->wp[w].lat, c->wp[w].lon,
                            opt->threshold_m, &all[w]) != 0)
            goto done;
        if (all[w].n == 0)
            choice[w] = -1;
        else if (opt->mode == SNAP_FIRST)
            choice[w] = 0;
        else if (opt->mode == SNAP_LAST)
            choice[w] = (int)all[w].n - 1;
        else /* nearest, and the ordered-mode fallback */
            choice[w] = pick_nearest(&all[w]);
    }
    if (opt->mode == SNAP_ORDERED &&
        choose_ordered(all, nw, choice, in_chain) != 0)
        goto done;

    for (size_t w = 0; w < nw; w++) {
        const struct waypoint *wp = &c->wp[w];

        if (choice[w] < 0) {
            r.n_skipped++;
            if (!opt->quiet) {
                double d = snap_nearest_offset(c, wp->lat, wp->lon);
                if (d < 0.0)
                    fprintf(stderr, "warning: waypoint '%s' skipped: "
                            "the track has no points\n", display_name(wp->name));
                else
                    fprintf(stderr, "warning: waypoint '%s' is %.0f m from the "
                            "track (limit %.0f m); skipped\n",
                            display_name(wp->name), d, opt->threshold_m);
            }
            continue;
        }
        if (opt->mode == SNAP_ORDERED && !in_chain[w]) {
            r.n_unordered++;
            if (!opt->quiet)
                fprintf(stderr, "warning: waypoint '%s' is out of GPX order "
                        "along the track; using its nearest pass\n",
                        display_name(wp->name));
        }
        if (add_course_point(c, wp, &all[w].v[choice[w]]) != 0)
            goto done;
        r.n_snapped++;
    }

    snap_sort_course_points(c);
    if (rep)
        *rep = r;
    rc = (opt->strict && r.n_skipped > 0) ? SNAP_STRICT : SNAP_OK;

done:
    if (all)
        for (size_t w = 0; w < nw; w++)
            snap_cands_free(&all[w]);
    free(all);
    free(choice);
    free(in_chain);
    snap_index_free(&ix);
    return rc;
}

void snap_print_summary(FILE *out, const struct course *c,
                        const struct snap_report *rep)
{
    for (size_t i = 0; i < c->n_cp; i++)
        fprintf(out, "  %-24s %7.2f km  offset %5.1f m\n",
                display_name(c->cp[i].name), c->cp[i].dist / 1000.0,
                c->cp[i].offset);
    if (rep)
        fprintf(out, "Snapped %zu of %zu waypoints (%zu skipped)\n",
                rep->n_snapped, rep->n_snapped + rep->n_skipped,
                rep->n_skipped);
}
