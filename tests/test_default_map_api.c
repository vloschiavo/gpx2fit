/* T4: the WASM bridge must expose the embedded default.map verbatim. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern const char *gpx2fit_get_default_map(void);

int main(void)
{
    const char *text = gpx2fit_get_default_map();
    assert(text != NULL);

    char expected[4096];
    FILE *f = fopen("data/default.map", "rb");
    assert(f != NULL);
    size_t n = fread(expected, 1, sizeof expected - 1, f);
    assert(!ferror(f));
    assert(n < sizeof expected - 1);
    assert(fclose(f) == 0);
    expected[n] = '\0';

    assert(strlen(text) == n);
    assert(memcmp(text, expected, n) == 0);
    assert(gpx2fit_get_default_map() == text);
    assert(strstr(text, "water   = water") != NULL);
    assert(strstr(text, "first aid = first_aid") != NULL);

    printf("test_default_map_api: getter matches data/default.map (%zu bytes)\n", n);
    return 0;
}
