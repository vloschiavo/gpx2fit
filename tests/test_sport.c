/* test_sport.c: tests for src/sport.c (task 11.1). */
#include <stdint.h>
#include <stdio.h>

#include "../src/sport.h"

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

/* Every documented name must resolve to the Profile 21.222 value. */
static void expect_ok(const char *name, uint8_t want_sport,
                      uint8_t want_sub)
{
    uint8_t sport = 0xAA, sub = 0xAA;

    checks++;
    if (!sport_parse(name, &sport, &sub)) {
        failures++;
        fprintf(stderr, "FAIL %s:%d: sport_parse(\"%s\") returned false\n",
                __FILE__, __LINE__, name ? name : "(null)");
        return;
    }
    CHECK(sport == want_sport);
    CHECK(sub == want_sub);
}

/* Unknown names must return false and leave both outputs alone. */
static void expect_bad(const char *name)
{
    uint8_t sport = 0xAA, sub = 0xAA;

    checks++;
    if (sport_parse(name, &sport, &sub)) {
        failures++;
        fprintf(stderr, "FAIL %s:%d: sport_parse(\"%s\") returned true\n",
                __FILE__, __LINE__, name ? name : "(null)");
    }
    CHECK(sport == 0xAA);
    CHECK(sub == 0xAA);
}

/* ---- 11.1: the seven activity types and their aliases ------------------- */

static void test_types_and_aliases(void)
{
    expect_ok("hiking", FIT_SPORT_HIKING, FIT_SUB_SPORT_GENERIC);
    expect_ok("running", FIT_SPORT_RUNNING, FIT_SUB_SPORT_GENERIC);
    expect_ok("trail-running", FIT_SPORT_RUNNING, FIT_SUB_SPORT_TRAIL);
    expect_ok("trail_running", FIT_SPORT_RUNNING, FIT_SUB_SPORT_TRAIL);
    expect_ok("cycling", FIT_SPORT_CYCLING, FIT_SUB_SPORT_GENERIC);
    expect_ok("road-cycling", FIT_SPORT_CYCLING, FIT_SUB_SPORT_ROAD);
    expect_ok("road_cycling", FIT_SPORT_CYCLING, FIT_SUB_SPORT_ROAD);
    expect_ok("gravel", FIT_SPORT_CYCLING, FIT_SUB_SPORT_GRAVEL_CYCLING);
    expect_ok("gravel-cycling", FIT_SPORT_CYCLING,
              FIT_SUB_SPORT_GRAVEL_CYCLING);
    expect_ok("gravel_cycling", FIT_SPORT_CYCLING,
              FIT_SUB_SPORT_GRAVEL_CYCLING);
    expect_ok("mtb", FIT_SPORT_CYCLING, FIT_SUB_SPORT_MOUNTAIN);
    expect_ok("mountain-biking", FIT_SPORT_CYCLING, FIT_SUB_SPORT_MOUNTAIN);
    expect_ok("mountain_biking", FIT_SPORT_CYCLING, FIT_SUB_SPORT_MOUNTAIN);
    expect_ok("other", FIT_SPORT_GENERIC, FIT_SUB_SPORT_GENERIC);
    expect_ok("generic", FIT_SPORT_GENERIC, FIT_SUB_SPORT_GENERIC);
}

/* Pin the numeric values themselves, not just the symbolic names. */
static void test_enum_values(void)
{
    CHECK(FIT_SPORT_GENERIC == 0);
    CHECK(FIT_SPORT_RUNNING == 1);
    CHECK(FIT_SPORT_CYCLING == 2);
    CHECK(FIT_SPORT_HIKING == 17);
    CHECK(FIT_SUB_SPORT_GENERIC == 0);
    CHECK(FIT_SUB_SPORT_TRAIL == 3);
    CHECK(FIT_SUB_SPORT_ROAD == 7);
    CHECK(FIT_SUB_SPORT_MOUNTAIN == 8);
    CHECK(FIT_SUB_SPORT_GRAVEL_CYCLING == 46);
}

static void test_case_insensitive(void)
{
    expect_ok("HIKING", 17, 0);
    expect_ok("RuNnInG", 1, 0);
    expect_ok("TRAIL-RUNNING", 1, 3);
    expect_ok("Trail_Running", 1, 3);
    expect_ok("CYCLING", 2, 0);
    expect_ok("ROAD-CYCLING", 2, 7);
    expect_ok("GrAvEl", 2, 46);
    expect_ok("GRAVEL_CYCLING", 2, 46);
    expect_ok("MTB", 2, 8);
    expect_ok("Mountain-Biking", 2, 8);
    expect_ok("OTHER", 0, 0);
    expect_ok("GeNeRiC", 0, 0);
}

static void test_whitespace(void)
{
    expect_ok("  hiking", 17, 0);
    expect_ok("hiking  ", 17, 0);
    expect_ok("\tHiking \r\n", 17, 0);
    expect_ok("  trail-running\t", 1, 3);
    expect_ok(" mountain_biking ", 2, 8);
}

static void test_invalid(void)
{
    uint8_t sport = 0xAA, sub = 0xAA;

    expect_bad(NULL);
    expect_bad("");
    expect_bad(" \t\r\n");
    expect_bad("walk");
    expect_bad("hike");
    expect_bad("trail");
    expect_bad("road");
    expect_bad("mountain");
    expect_bad("gravel bike");
    expect_bad("running!");
    expect_bad(" pre hiking");
    expect_bad("hiking post");
    expect_bad("hiking running");

    /* NULL or aliased outputs: rejected, with no partial write. */
    CHECK(!sport_parse("hiking", NULL, &sub) && sub == 0xAA);
    CHECK(!sport_parse("hiking", &sport, NULL) && sport == 0xAA);
    CHECK(!sport_parse("hiking", &sport, &sport) && sport == 0xAA);
    CHECK(!sport_parse("bogus", NULL, NULL));
}

int main(void)
{
    test_types_and_aliases();
    test_enum_values();
    test_case_insensitive();
    test_whitespace();
    test_invalid();

    if (failures) {
        fprintf(stderr, "test_sport: %d of %d checks FAILED\n", failures,
                checks);
        return 1;
    }
    printf("test_sport: all %d checks passed\n", checks);
    return 0;
}
