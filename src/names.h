/*
 * names.h: limits and safe truncation for names written into the FIT file.
 */
#ifndef NAMES_H
#define NAMES_H

#include <stddef.h>

/* Maximum length of a course_point name, in bytes, not counting the NUL.
 * PROVISIONAL: the FIT profile gives no maximum (NOTES.md 0.4). 15 is a limit
 * commonly quoted for watch displays. Verify on a device (tasks 5.4, 9.2). The
 * FIT string field is sized COURSE_POINT_NAME_MAX + 1 (the NUL). */
#define COURSE_POINT_NAME_MAX 15

/* Maximum length of the course name, in bytes, not counting the NUL. 90 is a
 * length known to work on the device; the true maximum is unknown, so revisit
 * it in task 9.1. (The Garmin reference file has a 31-byte name field.) */
#define COURSE_NAME_MAX 90

/* Copy src into dst (dst_size bytes in total), keeping at most max_bytes bytes
 * of src. The cut never lands inside a UTF-8 sequence: if max_bytes would split
 * a multibyte character, the whole character is dropped. dst is always
 * NUL-terminated (when dst_size > 0). The result also never exceeds
 * dst_size - 1 bytes.
 *
 * dst may equal src (in-place truncation). Other overlaps are also safe.
 * A NULL src gives an empty string. Returns the length of the result in bytes
 * (not counting the NUL). With dst == NULL or dst_size == 0 nothing is written
 * and 0 is returned.
 *
 * src is assumed to be valid UTF-8 (expat guarantees this for GPX text). */
size_t utf8_truncate(char *dst, size_t dst_size, const char *src,
                     size_t max_bytes);

#endif /* NAMES_H */
