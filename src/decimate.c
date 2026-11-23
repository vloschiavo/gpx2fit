/* decimate.c: --max-points track reduction (task 8.2). See decimate.h. */
#include "decimate.h"

#include <stdint.h>
#include <stdlib.h>

#include "geo.h"

/* A run of dropped points between two kept points, lo and hi (indices).
 * far is the interior point farthest from the line lo-hi, dev its distance. */
struct interval {
    size_t lo, hi;
    size_t far;
    double dev; /* meters */
};

/* Max-heap order: larger deviation first; on equal deviation the interval
 * nearer the start of the track, so the result is deterministic. */
static bool before(const struct interval *a, const struct interval *b)
{
    if (a->dev != b->dev)
        return a->dev > b->dev;
    return a->lo < b->lo;
}

static void heap_push(struct interval *h, size_t *n, struct interval iv)
{
    size_t i = (*n)++;

    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (!before(&iv, &h[parent]))
            break;
        h[i] = h[parent];
        i = parent;
    }
    h[i] = iv;
}

static struct interval heap_pop(struct interval *h, size_t *n)
{
    struct interval top = h[0];
    struct interval last = h[--(*n)];
    size_t i = 0;

    while (2 * i + 1 < *n) {
        size_t child = 2 * i + 1;
        if (child + 1 < *n && before(&h[child + 1], &h[child]))
            child++;
        if (!before(&h[child], &last))
            break;
        h[i] = h[child];
        i = child;
    }
    if (*n > 0)
        h[i] = last;
    return top;
}

/* Fill *iv for the run between kept points lo and hi. Returns false when
 * there is no interior point (nothing could be added). */
static bool make_interval(const struct course *c, size_t lo, size_t hi,
                          struct interval *iv)
{
    const struct trackpoint *tp = c->tp;
    size_t far = 0;
    double best = -1.0;

    if (hi - lo < 2)
        return false;
    for (size_t k = lo + 1; k < hi; k++) {
        struct seg_proj sp = project_to_segment(tp[k].lat, tp[k].lon,
                                                tp[lo].lat, tp[lo].lon,
                                                tp[hi].lat, tp[hi].lon);
        if (sp.dist_m > best) {
            best = sp.dist_m;
            far = k;
        }
    }
    iv->lo = lo;
    iv->hi = hi;
    iv->far = far;
    iv->dev = best;
    return true;
}

/* Index of the trackpoint whose dist is closest to d (lower index on a tie).
 * tp[].dist is non-decreasing. Requires n >= 1. */
static size_t nearest_by_dist(const struct trackpoint *tp, size_t n, double d)
{
    size_t lo = 0, hi = n; /* first index with tp[i].dist >= d */

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (tp[mid].dist < d)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == 0)
        return 0;
    if (lo == n)
        return n - 1;
    return (d - tp[lo - 1].dist <= tp[lo].dist - d) ? lo - 1 : lo;
}

int course_decimate(struct course *c, size_t max_points,
                    struct decimate_report *rep)
{
    struct decimate_report r = { c->n_tp, c->n_tp, 0, false };
    size_t n = c->n_tp;
    uint8_t *keep = NULL;
    struct interval *heap = NULL;
    size_t n_heap = 0;

    if (n <= max_points || n < 2) {
        r.n_required = n < 2 ? n : 0;
        goto done;
    }

    keep = calloc(n, 1);
    if (!keep)
        return -1;

    keep[0] = 1;
    keep[n - 1] = 1;
    for (size_t i = 0; i < c->n_cp; i++)
        keep[nearest_by_dist(c->tp, n, c->cp[i].dist)] = 1;
    for (size_t i = 0; i < n; i++)
        r.n_required += keep[i];

    if (r.n_required >= max_points) {
        r.exceeded = r.n_required > max_points;
    } else {
        /* One interval per gap between consecutive required points; there
         * are never more intervals than kept points, so max_points slots
         * are enough. */
        heap = malloc(max_points * sizeof *heap);
        if (!heap) {
            free(keep);
            return -1;
        }
        size_t prev = 0;
        for (size_t i = 1; i < n; i++) {
            struct interval iv;

            if (!keep[i])
                continue;
            if (make_interval(c, prev, i, &iv))
                heap_push(heap, &n_heap, iv);
            prev = i;
        }
        for (size_t budget = max_points - r.n_required;
             budget > 0 && n_heap > 0; budget--) {
            struct interval top = heap_pop(heap, &n_heap);
            struct interval iv;

            keep[top.far] = 1;
            if (make_interval(c, top.lo, top.far, &iv))
                heap_push(heap, &n_heap, iv);
            if (make_interval(c, top.far, top.hi, &iv))
                heap_push(heap, &n_heap, iv);
        }
    }

    /* Compact the kept points to the front, keeping their order. */
    size_t out = 0;
    for (size_t i = 0; i < n; i++)
        if (keep[i])
            c->tp[out++] = c->tp[i];
    c->n_tp = out;
    r.n_after = out;

done:
    free(keep);
    free(heap);
    if (rep)
        *rep = r;
    return 0;
}
