/* diag.c: structured warning / error collector (task 13.2). */
#include "diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void diag_list_init(struct diag_list *d)
{
    if (!d)
        return;
    d->items = NULL;
    d->n = 0;
    d->cap = 0;
}

void diag_list_free(struct diag_list *d)
{
    if (!d)
        return;
    for (size_t i = 0; i < d->n; i++)
        free(d->items[i]);
    free(d->items);
    diag_list_init(d);
}

int diag_list_vadd(struct diag_list *d, const char *fmt, va_list ap)
{
    if (!d || !fmt)
        return -1;

    va_list ap2;
    va_copy(ap2, ap);
    int len = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (len < 0)
        return -1;

    char *buf = malloc((size_t)len + 1);
    if (!buf)
        return -1;
    va_list ap3;
    va_copy(ap3, ap);
    vsnprintf(buf, (size_t)len + 1, fmt, ap3);
    va_end(ap3);

    if (d->n == d->cap) {
        size_t ncap = d->cap ? d->cap * 2 : 8;
        char **ni = realloc(d->items, ncap * sizeof *ni);
        if (!ni) {
            free(buf);
            return -1;
        }
        d->items = ni;
        d->cap = ncap;
    }
    d->items[d->n++] = buf;
    return 0;
}

int diag_list_add(struct diag_list *d, const char *fmt, ...)
{
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = diag_list_vadd(d, fmt, ap);
    va_end(ap);
    return rc;
}
