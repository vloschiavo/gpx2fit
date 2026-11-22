/* course.c: course lifetime and growable-array helpers. */
#include "course.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 64

/* Make room for one more element in a growable array. Doubles capacity,
 * guarding against size_t overflow. Returns 0 on success, -1 on failure
 * (in which case *items, *cap are unchanged). */
static int grow(void **items, size_t *cap, size_t n, size_t elem_size)
{
    if (n < *cap)
        return 0;

    size_t new_cap = *cap ? *cap * 2 : INITIAL_CAP;
    if (new_cap < *cap || new_cap > SIZE_MAX / elem_size)
        return -1;

    void *p = realloc(*items, new_cap * elem_size);
    if (!p)
        return -1;

    *items = p;
    *cap = new_cap;
    return 0;
}

/* strdup is not C11, so keep a private copy helper. NULL in, NULL out. */
static char *dup_string(const char *s)
{
    if (!s)
        return NULL;
    size_t len = strlen(s) + 1;
    char *d = malloc(len);
    if (d)
        memcpy(d, s, len);
    return d;
}

void course_init(struct course *c)
{
    memset(c, 0, sizeof *c);
}

void course_free(struct course *c)
{
    for (size_t i = 0; i < c->n_wp; i++)
        free(c->wp[i].name);
    for (size_t i = 0; i < c->n_cp; i++)
        free(c->cp[i].name);

    free(c->name);
    free(c->tp);
    free(c->wp);
    free(c->cp);

    course_init(c);
}

int course_add_trackpoint(struct course *c, const struct trackpoint *tp)
{
    void *items = c->tp;
    if (grow(&items, &c->cap_tp, c->n_tp, sizeof *c->tp) != 0)
        return -1;
    c->tp = items;
    c->tp[c->n_tp++] = *tp;
    return 0;
}

int course_add_waypoint(struct course *c, const char *name,
                        double lat, double lon)
{
    char *copy = dup_string(name);
    if (name && !copy)
        return -1;

    void *items = c->wp;
    if (grow(&items, &c->cap_wp, c->n_wp, sizeof *c->wp) != 0) {
        free(copy);
        return -1;
    }
    c->wp = items;

    struct waypoint *w = &c->wp[c->n_wp++];
    w->name = copy;
    w->lat = lat;
    w->lon = lon;
    return 0;
}
