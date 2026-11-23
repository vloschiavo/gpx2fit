/*
 * sport.h: activity name -> FIT course.sport / course.sub_sport (task 11.1).
 *
 * The values below come from the official Garmin FIT C++ SDK, tag 21.222.0,
 * file src/fit_profile.hpp ("Profile Version = 21.222.0Release"):
 *   https://github.com/garmin/fit-cpp-sdk/blob/21.222.0/src/fit_profile.hpp
 * FIT_SPORT_GENERIC = 0, RUNNING = 1, CYCLING = 2, HIKING = 17;
 * FIT_SUB_SPORT_GENERIC = 0, TRAIL = 3, ROAD = 7, MOUNTAIN = 8,
 * GRAVEL_CYCLING = 46. course.sport is field 4 and course.sub_sport is
 * field 7 (src/fit_course_mesg.hpp at the same tag).
 *
 * The two constants also present in fit_write.h are guarded so that both
 * headers may be included in either order.
 */
#ifndef SPORT_H
#define SPORT_H

#include <stdbool.h>
#include <stdint.h>

/* FIT sport enum (Profile 21.222). */
#define FIT_SPORT_GENERIC  0
#define FIT_SPORT_RUNNING  1
#define FIT_SPORT_CYCLING  2
#ifndef FIT_SPORT_HIKING
#define FIT_SPORT_HIKING  17
#endif

/* FIT sub_sport enum (Profile 21.222). */
#ifndef FIT_SUB_SPORT_GENERIC
#define FIT_SUB_SPORT_GENERIC    0
#endif
#define FIT_SUB_SPORT_TRAIL      3
#define FIT_SUB_SPORT_ROAD       7
#define FIT_SUB_SPORT_MOUNTAIN   8
#define FIT_SUB_SPORT_GRAVEL_CYCLING 46

/* Resolve an activity name to a FIT sport and sub_sport pair.
 *
 * Matching is ASCII case-insensitive; '-' and '_' are interchangeable and
 * both mean a space, so "trail-running" and "TRAIL_Running" are the same
 * name. Leading/trailing ASCII whitespace (space, tab, newline, vertical
 * tab, form feed, carriage return) is ignored.
 *
 * Returns true and writes both outputs on a recognized name. Returns false
 * for NULL, empty/whitespace-only, unknown or overlong names, and leaves
 * both outputs untouched. sport and sub_sport must be non-NULL and distinct;
 * if either is not, the function returns false without writing anything.
 */
bool sport_parse(const char *str, uint8_t *sport, uint8_t *sub_sport);

#endif /* SPORT_H */
