/* sport.c: activity name -> FIT course.sport / course.sub_sport (task 11.1). */
#include "sport.h"

#include <stddef.h>
#include <string.h>

/*
 * Verified against the official Garmin FIT C++ SDK, tag 21.222.0,
 * src/fit_profile.hpp (Profile Version = 21.222.0Release), lines defining
 * FIT_SPORT_* and FIT_SUB_SPORT_*:
 *   https://github.com/garmin/fit-cpp-sdk/blob/21.222.0/src/fit_profile.hpp
 *
 * FIT_SPORT: generic 0, running 1, cycling 2, hiking 17.
 * FIT_SUB_SPORT: generic 0, trail 3, road 7, mountain 8,
 *                gravel_cycling 46.
 *
 * The plan allowed generic (0) or road (7) for "cycling"/"road-cycling";
 * plain "cycling" uses generic and the explicit road alias uses road (7).
 * All canonical names below use spaces as separators; the parser folds '-'
 * and '_' to spaces, so both spellings of each alias are accepted.
 */
struct sport_entry {
    const char *name;
    uint8_t     sport;
    uint8_t     sub_sport;
};

static const struct sport_entry sport_table[] = {
    { "hiking",          FIT_SPORT_HIKING,  FIT_SUB_SPORT_GENERIC },
    { "running",         FIT_SPORT_RUNNING, FIT_SUB_SPORT_GENERIC },
    { "trail running",   FIT_SPORT_RUNNING, FIT_SUB_SPORT_TRAIL },
    { "cycling",         FIT_SPORT_CYCLING, FIT_SUB_SPORT_GENERIC },
    { "road cycling",    FIT_SPORT_CYCLING, FIT_SUB_SPORT_ROAD },
    { "gravel",          FIT_SPORT_CYCLING, FIT_SUB_SPORT_GRAVEL_CYCLING },
    { "gravel cycling",  FIT_SPORT_CYCLING, FIT_SUB_SPORT_GRAVEL_CYCLING },
    { "mtb",             FIT_SPORT_CYCLING, FIT_SUB_SPORT_MOUNTAIN },
    { "mountain biking", FIT_SPORT_CYCLING, FIT_SUB_SPORT_MOUNTAIN },
    { "other",           FIT_SPORT_GENERIC, FIT_SUB_SPORT_GENERIC },
    { "generic",         FIT_SPORT_GENERIC, FIT_SUB_SPORT_GENERIC }
};

static int lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Fold case and both separator characters to a space. */
static int norm(unsigned char c)
{
    c = (unsigned char)lower(c);
    return (c == '-' || c == '_') ? ' ' : c;
}

static bool is_blank(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' ||
           c == '\r';
}

/* Compare the trimmed input bytes a[0..alen) with a canonical table name. */
static bool name_equal(const char *a, size_t alen, const char *b)
{
    size_t i;

    for (i = 0; i < alen && b[i]; i++) {
        if (norm((unsigned char)a[i]) != norm((unsigned char)b[i]))
            return false;
    }
    return i == alen && b[i] == '\0';
}

bool sport_parse(const char *str, uint8_t *sport, uint8_t *sub_sport)
{
    size_t len;

    if (!str || !sport || !sub_sport || sport == sub_sport)
        return false;
    len = strlen(str);
    while (len > 0 && is_blank((unsigned char)str[0])) {
        str++;
        len--;
    }
    while (len > 0 && is_blank((unsigned char)str[len - 1]))
        len--;
    if (len == 0)
        return false;

    for (size_t i = 0; i < sizeof sport_table / sizeof *sport_table; i++) {
        if (name_equal(str, len, sport_table[i].name)) {
            *sport = sport_table[i].sport;
            *sub_sport = sport_table[i].sub_sport;
            return true;
        }
    }
    return false;
}
