/* test_names.c: tests for src/names.c (task 5.5). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/names.h"

static int failures;
static int checks;

#define CHECK(cond)                                                        \
    do {                                                                   \
        checks++;                                                          \
        if (!(cond)) {                                                     \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                  \
    } while (0)

#define CHECK_STR(a, b)                                                    \
    do {                                                                   \
        const char *a_ = (a), *b_ = (b);                                   \
        checks++;                                                          \
        if (strcmp(a_, b_) != 0) {                                         \
            failures++;                                                    \
            fprintf(stderr, "FAIL %s:%d: %s = \"%s\", expected \"%s\"\n",  \
                    __FILE__, __LINE__, #a, a_, b_);                       \
        }                                                                  \
    } while (0)

/* Is s well-formed UTF-8 (no split sequences, no stray continuation bytes)? */
static int valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p) {
        int extra;

        if (*p < 0x80)
            extra = 0;
        else if ((*p & 0xE0) == 0xC0)
            extra = 1;
        else if ((*p & 0xF0) == 0xE0)
            extra = 2;
        else if ((*p & 0xF8) == 0xF0)
            extra = 3;
        else
            return 0;
        p++;
        while (extra--) {
            if ((*p & 0xC0) != 0x80)
                return 0;
            p++;
        }
    }
    return 1;
}

/* Truncate src to max bytes into a poisoned 128-byte buffer, check the
 * universal properties, and return the result length. */
static size_t trunc_into(char *buf, const char *src, size_t max)
{
    size_t n;

    memset(buf, 0xFF, 128);
    n = utf8_truncate(buf, 128, src, max);
    CHECK(n <= max);
    CHECK(strlen(buf) == n);   /* NUL right after the result */
    CHECK(valid_utf8(buf));
    return n;
}

/* Run the same cases against a limit; lim is the byte limit. */
static void run_limit(size_t lim)
{
    char buf[128], src[256];
    const char *e = "\xC3\xA9";          /* e-acute, 2 bytes */
    const char *euro = "\xE2\x82\xAC";   /* euro sign, 3 bytes */
    const char *grin = "\xF0\x9F\x98\x80"; /* emoji, 4 bytes */

    /* ASCII: shorter, exactly at the limit, one over, far over. */
    memset(src, 'a', lim - 1); src[lim - 1] = '\0';
    CHECK(trunc_into(buf, src, lim) == lim - 1);
    CHECK(strcmp(buf, src) == 0);

    memset(src, 'a', lim); src[lim] = '\0';
    CHECK(trunc_into(buf, src, lim) == lim);       /* exact length: kept whole */
    CHECK(strcmp(buf, src) == 0);

    memset(src, 'a', lim + 1); src[lim + 1] = '\0';
    CHECK(trunc_into(buf, src, lim) == lim);
    CHECK(strncmp(buf, src, lim) == 0);

    memset(src, 'a', 120); src[120] = '\0';
    CHECK(trunc_into(buf, src, lim) == lim);

    /* A 2-byte character straddling the limit is dropped whole... */
    memset(src, 'a', lim - 1); src[lim - 1] = '\0';
    strcat(src, e);                                /* ends at byte lim + 1 */
    CHECK(trunc_into(buf, src, lim) == lim - 1);
    CHECK(strncmp(buf, src, lim - 1) == 0);

    /* ...but kept when it fits exactly. */
    memset(src, 'a', lim - 2); src[lim - 2] = '\0';
    strcat(src, e);                                /* ends at byte lim */
    CHECK(trunc_into(buf, src, lim) == lim);
    CHECK(strcmp(buf, src) == 0);

    /* 3-byte: cut after 1 byte of it and after 2 bytes of it. */
    for (size_t pre = lim - 2; pre <= lim - 1; pre++) {
        memset(src, 'a', pre); src[pre] = '\0';
        strcat(src, euro);
        CHECK(trunc_into(buf, src, lim) == pre);
    }
    /* 3-byte exactly fitting. */
    memset(src, 'a', lim - 3); src[lim - 3] = '\0';
    strcat(src, euro);
    CHECK(trunc_into(buf, src, lim) == lim);

    /* 4-byte: cut after 1, 2 and 3 bytes of it. */
    for (size_t pre = lim - 3; pre <= lim - 1; pre++) {
        memset(src, 'a', pre); src[pre] = '\0';
        strcat(src, grin);
        CHECK(trunc_into(buf, src, lim) == pre);
    }

    /* A string of nothing but multibyte characters. */
    src[0] = '\0';
    for (int i = 0; i < 60; i++)
        strcat(src, euro);
    CHECK(trunc_into(buf, src, lim) == (lim / 3) * 3);
}

static void test_both_limits(void)
{
    run_limit(COURSE_POINT_NAME_MAX);
    run_limit(COURSE_NAME_MAX);
}

static void test_constants(void)
{
    CHECK(COURSE_NAME_MAX == 90);
    CHECK(COURSE_POINT_NAME_MAX >= 1 && COURSE_POINT_NAME_MAX < COURSE_NAME_MAX);
}

static void test_edges(void)
{
    char buf[128];
    char small[4];
    char inplace[32];

    /* Empty string, zero limit. */
    CHECK(trunc_into(buf, "", 15) == 0);
    CHECK(trunc_into(buf, "abc", 0) == 0);
    CHECK_STR(buf, "");
    CHECK(trunc_into(buf, "\xC3\xA9", 1) == 0);   /* 1 byte cannot hold it */

    /* NULL src gives an empty string. */
    memset(buf, 0xFF, sizeof buf);
    CHECK(utf8_truncate(buf, sizeof buf, NULL, 15) == 0);
    CHECK_STR(buf, "");

    /* dst smaller than max: the buffer size wins, still terminated. */
    memset(small, 0xFF, sizeof small);
    CHECK(utf8_truncate(small, sizeof small, "abcdefgh", 15) == 3);
    CHECK_STR(small, "abc");
    memset(small, 0xFF, sizeof small);
    CHECK(utf8_truncate(small, sizeof small, "ab\xC3\xA9" "z", 15) == 2);
    CHECK_STR(small, "ab");
    memset(small, 0xFF, sizeof small);
    CHECK(utf8_truncate(small, 1, "abc", 15) == 0);
    CHECK(small[0] == '\0');

    /* Nothing is written when there is no room at all. */
    memset(small, 'x', sizeof small);
    CHECK(utf8_truncate(small, 0, "abc", 15) == 0);
    CHECK(small[0] == 'x');
    CHECK(utf8_truncate(NULL, 10, "abc", 15) == 0);

    /* In place. */
    strcpy(inplace, "Water Stop Number Two");
    CHECK(utf8_truncate(inplace, sizeof inplace, inplace, 15) == 15);
    CHECK_STR(inplace, "Water Stop Numb");
    strcpy(inplace, "abcdefghijklmn\xC3\xA9");     /* 14 + 2 bytes */
    CHECK(utf8_truncate(inplace, strlen(inplace) + 1, inplace, 15) == 14);
    CHECK_STR(inplace, "abcdefghijklmn");
    strcpy(inplace, "short");
    CHECK(utf8_truncate(inplace, sizeof inplace, inplace, 15) == 5);
    CHECK_STR(inplace, "short");

    /* Real names from the sample. */
    CHECK(trunc_into(buf, "Ranger Station", COURSE_POINT_NAME_MAX) == 14);
    CHECK_STR(buf, "Ranger Station");
}

int main(void)
{
    test_constants();
    test_both_limits();
    test_edges();

    if (failures) {
        fprintf(stderr, "test_names: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_names: all %d checks passed\n", checks);
    return 0;
}
