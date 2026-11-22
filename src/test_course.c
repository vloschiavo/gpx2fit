/* test_course.c: course memory management (task 1.3). Run under ASan/UBSan. */
#include "course.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 10000

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void test_empty(void)
{
    struct course c;
    course_init(&c);
    CHECK(c.tp == NULL && c.wp == NULL && c.cp == NULL && c.name == NULL);
    CHECK(c.n_tp == 0 && c.n_wp == 0 && c.n_cp == 0);
    course_free(&c);   /* freeing an empty course is fine */
    course_free(&c);   /* and so is freeing twice */
}

static void test_many_points(void)
{
    struct course c;
    course_init(&c);

    for (int i = 0; i < N; i++) {
        struct trackpoint tp = { .lat = i * 0.001, .lon = -i * 0.001,
                                 .ele = i, .has_ele = (i % 2 == 0) };
        CHECK(course_add_trackpoint(&c, &tp) == 0);

        char name[32];
        snprintf(name, sizeof name, "wp %d", i);
        CHECK(course_add_waypoint(&c, name, i * 0.5, i * -0.5) == 0);
    }
    CHECK(course_add_waypoint(&c, NULL, 1.0, 2.0) == 0);  /* unnamed */

    CHECK(c.n_tp == N);
    CHECK(c.n_wp == N + 1);
    CHECK(c.cap_tp >= c.n_tp && c.cap_wp >= c.n_wp);

    /* Data survived all the reallocations. */
    CHECK(c.tp[0].lat == 0.0);
    CHECK(c.tp[N - 1].ele == N - 1 && !c.tp[N - 1].has_ele);
    CHECK(c.tp[N - 2].has_ele);
    CHECK(strcmp(c.wp[0].name, "wp 0") == 0);
    CHECK(strcmp(c.wp[N - 1].name, "wp 9999") == 0);
    CHECK(c.wp[N].name == NULL && c.wp[N].lon == 2.0);

    course_free(&c);
    CHECK(c.tp == NULL && c.wp == NULL && c.n_tp == 0 && c.n_wp == 0);
    CHECK(c.cap_tp == 0 && c.cap_wp == 0);
}

static void test_owned_strings_and_cp(void)
{
    /* course_free must also release the course name and course point names.
     * No add helper exists for these yet, so fill them in by hand. */
    struct course c;
    course_init(&c);

    c.name = malloc(8);
    CHECK(c.name != NULL);
    if (c.name)
        strcpy(c.name, "My Trek");

    c.cp = calloc(2, sizeof *c.cp);
    CHECK(c.cp != NULL);
    if (c.cp) {
        c.cap_cp = c.n_cp = 2;
        c.cp[0].name = malloc(6);
        if (c.cp[0].name)
            strcpy(c.cp[0].name, "Water");
        /* cp[1].name stays NULL: free(NULL) must be fine */
    }

    course_free(&c);
    CHECK(c.name == NULL && c.cp == NULL && c.n_cp == 0);
}

int main(void)
{
    test_empty();
    test_many_points();
    test_owned_strings_and_cp();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("test_course: all checks passed");
    return 0;
}
