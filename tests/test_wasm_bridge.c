/* T1: the WASM bridge must forward `track`/`stitch` to the core GPX reader,
 * reusing its stitching and single-track selection instead of reimplementing
 * them. Linked against all of src/ except main.c by `make test`. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void *gpx2fit_convert(const char *gpx, const char *map, const char *name,
                             uint8_t sport, uint8_t sub_sport, double snap_m,
                             const char *pass_mode, double pace_mps,
                             uint32_t start_fit, long max_points, int strict,
                             long track, int stitch, const char *overrides_json);
extern uint8_t *gpx2fit_result_data(void *result);
extern size_t gpx2fit_result_len(void *result);
extern const char *gpx2fit_result_json(void *result);
extern int gpx2fit_result_rc(void *result);
extern void gpx2fit_free(void *result);

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    assert(fseek(f, 0, SEEK_END) == 0);
    long length = ftell(f);
    assert(length > 0);
    assert(fseek(f, 0, SEEK_SET) == 0);
    char *text = malloc((size_t)length + 1);
    assert(text != NULL);
    assert(fread(text, 1, (size_t)length, f) == (size_t)length);
    text[length] = '\0';
    assert(fclose(f) == 0);
    return text;
}

static void *convert_with(const char *gpx, const char *name, long track,
                          int stitch, double snap_m, const char *overrides)
{
    void *result = gpx2fit_convert(gpx, "", name, 17, 0, snap_m, "nearest",
                                   5.0 / 3.6, 1104624000, 0, 0, track, stitch,
                                   overrides);
    assert(result != NULL);
    return result;
}

#define convert(gpx, name, track, stitch) \
    convert_with((gpx), (name), (track), (stitch), 2000.0, NULL)

static void expect_json(void *result, const char *needle)
{
    const char *json = gpx2fit_result_json(result);
    assert(json != NULL);
    assert(strstr(json, needle) != NULL);
}

/* T2: the bridge applies per-waypoint overrides to snapping, preview JSON,
 * and the FIT bytes. Waypoint 0 is near the track; waypoint 1 is ~1.1 km away. */
static void test_overrides(void)
{
    const char *gpx =
        "<?xml version=\"1.0\"?><gpx version=\"1.1\" "
        "xmlns=\"http://www.topografix.com/GPX/1/1\">"
        "<wpt lat=\"0.001\" lon=\"0\"><name>Near</name></wpt>"
        "<wpt lat=\"0.01\" lon=\"0\"><name>Far</name></wpt>"
        "<trk><trkseg><trkpt lat=\"0\" lon=\"0\"/><trkpt lat=\"0\" lon=\"0.01\"/>"
        "</trkseg></trk></gpx>";

    void *automatic = convert_with(gpx, NULL, 0, 1, 250.0, "[]");
    assert(gpx2fit_result_rc(automatic) == 0);
    expect_json(automatic, "\"index\":0,\"status\":\"snapped\",\"overridden\":false");
    expect_json(automatic, "\"index\":1,\"status\":\"skipped\",\"overridden\":false");
    expect_json(automatic, "\"index\":0,\"name\":\"Near\""); /* skipped list */
    expect_json(automatic, "\"index\":1,\"name\":\"Far\"");  /* course points */

    void *forced = convert_with(gpx, NULL, 0, 1, 250.0, "[2,1]");
    assert(gpx2fit_result_rc(forced) == 0);
    expect_json(forced, "\"index\":0,\"status\":\"skipped\",\"overridden\":true");
    expect_json(forced, "\"index\":1,\"status\":\"snapped\",\"overridden\":true");
    expect_json(forced, "\"index\":0,\"name\":\"Near\""); /* still skipped */
    expect_json(forced, "\"waypoints_snapped\":1,");

    size_t auto_len = gpx2fit_result_len(automatic);
    size_t forced_len = gpx2fit_result_len(forced);
    assert(auto_len > 0 && forced_len > 0);
    assert(auto_len != forced_len
           || memcmp(gpx2fit_result_data(automatic),
                     gpx2fit_result_data(forced), auto_len) != 0);

    /* Malformed override input is rejected before any file is produced. */
    void *bad = convert_with(gpx, NULL, 0, 1, 250.0, "[1,");
    assert(gpx2fit_result_rc(bad) == 1);
    expect_json(bad, "invalid waypoint overrides");
    gpx2fit_free(bad);
    gpx2fit_free(forced);
    gpx2fit_free(automatic);
}

int main(void)
{
    char *multi = read_file("tests/data/multi_track.gpx");
    char *single = read_file("tests/data/gpx11_basic.gpx");

    /* stitch=1: core joins all <trk>s, anchors on "Start", snaps all waypoints. */
    void *joined = convert(multi, NULL, 0, 1);
    assert(gpx2fit_result_rc(joined) == 0);
    assert(gpx2fit_result_data(joined) != NULL && gpx2fit_result_len(joined) > 0);
    expect_json(joined, "\"stitch\":true");
    expect_json(joined, "\"track\":0");
    expect_json(joined, "\"track_points\":9");
    expect_json(joined, "\"course_points\":3");
    expect_json(joined, "\"course_name\":\"Head\"");
    expect_json(joined, "\"name\":\"Start\"");
    expect_json(joined, "\"name\":\"Water\"");
    expect_json(joined, "\"name\":\"Camp\"");
    gpx2fit_free(joined);

    /* stitch=0 + track=N: core reads only that <trk>, keeping its own name. */
    void *middle = convert(multi, NULL, 2, 0);
    assert(gpx2fit_result_rc(middle) == 0);
    assert(gpx2fit_result_data(middle) != NULL && gpx2fit_result_len(middle) > 0);
    expect_json(middle, "\"stitch\":false");
    expect_json(middle, "\"track\":2");
    expect_json(middle, "\"track_points\":3");
    expect_json(middle, "\"course_points\":1");
    expect_json(middle, "\"course_name\":\"Middle\"");
    expect_json(middle, "\"name\":\"Water\"");
    gpx2fit_free(middle);

    /* A user-supplied name still overrides the per-track name when splitting. */
    void *named = convert(multi, "My Course Middle", 2, 0);
    assert(gpx2fit_result_rc(named) == 0);
    expect_json(named, "\"course_name\":\"My Course Middle\"");
    gpx2fit_free(named);

    /* Single-track input is unaffected by either setting. */
    void *one = convert(single, NULL, 0, 1);
    assert(gpx2fit_result_rc(one) == 0);
    expect_json(one, "\"track_points\":3");
    expect_json(one, "\"course_name\":\"Basic Track\"");
    gpx2fit_free(one);

    /* The CLI's mutually-exclusive options are enforced in the bridge too. */
    void *bad = convert(multi, NULL, 2, 1);
    assert(gpx2fit_result_rc(bad) == 1);
    expect_json(bad, "choose either one track or stitching");
    gpx2fit_free(bad);

    bad = convert(multi, NULL, -1, 0);
    assert(gpx2fit_result_rc(bad) == 1);
    expect_json(bad, "invalid track index");
    gpx2fit_free(bad);

    test_overrides();
    free(multi);
    free(single);
    printf("test_wasm_bridge: track/stitch bridge parameters, core reuse, and "
           "waypoint overrides passed\n");
    return 0;
}
