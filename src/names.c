/* names.c: safe truncation of UTF-8 names (task 5.5). */
#include "names.h"

#include <string.h>

size_t utf8_truncate(char *dst, size_t dst_size, const char *src,
                     size_t max_bytes)
{
    size_t len, n;

    if (!dst || dst_size == 0)
        return 0;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }

    len = strlen(src);
    n = len;
    if (n > max_bytes)
        n = max_bytes;
    if (n > dst_size - 1)
        n = dst_size - 1;

    /* If we cut the string short and src[n] is a continuation byte
     * (10xxxxxx), the cut is inside a character: back up to its lead byte,
     * which then falls outside the kept part. */
    if (n < len) {
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80)
            n--;
    }

    memmove(dst, src, n);
    dst[n] = '\0';
    return n;
}
