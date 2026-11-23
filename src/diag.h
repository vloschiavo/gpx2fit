/*
 * diag.h: structured warning / error collector (task 13.2).
 *
 * A diag_list is an optional, growable list of human-readable messages
 * produced by the library functions. When a caller passes a non-NULL
 * diag_list, warnings (and other diagnostics) are appended to it as owned
 * strings, so that environments without a stderr (e.g. WebAssembly) can
 * surface them. Library functions still print to stderr according to their
 * `quiet` flags when a diag_list is NULL, preserving the CLI behaviour.
 */
#ifndef DIAG_H
#define DIAG_H

#include <stddef.h>
#include <stdarg.h>

struct diag_list {
    char **items;   /* owned, NUL-terminated strings */
    size_t n;       /* number in use */
    size_t cap;     /* allocated slots */
};

/* Initialise an empty list. Does not allocate. */
void diag_list_init(struct diag_list *d);

/* Free all owned strings and reset the list. Safe on a zero-initialised list. */
void diag_list_free(struct diag_list *d);

/* Append a printf-style message. Returns 0 on success, -1 on allocation failure
 * (the list is left unchanged on failure). */
int diag_list_add(struct diag_list *d, const char *fmt, ...);

/* Variadic helper used by diag_list_add. */
int diag_list_vadd(struct diag_list *d, const char *fmt, va_list ap);

#endif /* DIAG_H */
