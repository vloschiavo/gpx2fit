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
                             long track, int stitch);
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

static void *convert(const char *gpx, const char *name, long track, int stitch)
{
    void *result = gpx2fit_convert(gpx, "", name, 17, 0, 2000.0, "nearest",
                                   5.0 / 3.6, 1104624000, 0, 0, track, stitch);
    assert(result != NULL);
    return result;
}

static void expect_json(void *result, const char *needle)
{
    const char *json = gpx2fit_result_json(result);
    assert(json != NULL);
    assert(strstr(json, needle) != NULL);
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

    free(multi);
    free(single);
    printf("test_wasm_bridge: track/stitch bridge parameters and core reuse passed\n");
    return 0;
}
