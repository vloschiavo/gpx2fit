/*
 * fuzz_gpx.c: fuzz harness for the GPX reader (task 10.2).
 *
 * Standard libFuzzer entry point. Build with clang -fsanitize=fuzzer (see
 * `make fuzz-libfuzzer`), or link with tests/fuzz/fuzz_driver.c for a
 * gcc-only mutation driver (`make fuzz`).
 *
 * read_gpx() takes a path, so each input is written to one temp file that is
 * created once and rewritten per input. Each input is read twice: once with
 * track = 0 (pick the longest) and once with a track index taken from the
 * first input byte (1..4), so the "chosen track" paths are exercised too.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "course.h"
#include "geo.h"
#include "gpx.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static char tmp_path[64];

static void cleanup(void)
{
    if (tmp_path[0])
        unlink(tmp_path);
}

static int write_input(const uint8_t *data, size_t size)
{
    if (!tmp_path[0]) {
        char tmpl[] = "/tmp/fuzz_gpx_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) {
            perror("mkstemp");
            abort();
        }
        close(fd);
        memcpy(tmp_path, tmpl, sizeof tmpl);
        atexit(cleanup);
    }
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        perror(tmp_path);
        abort();
    }
    if (size && fwrite(data, 1, size, f) != size) {
        perror("fwrite");
        abort();
    }
    if (fclose(f) != 0) {
        perror("fclose");
        abort();
    }
    return 0;
}

static void run_once(long track)
{
    struct course c;
    struct gpx_options opt = { .track = track, .quiet = true };
    char err[256];

    course_init(&c);
    if (read_gpx(tmp_path, &c, &opt, err, sizeof err) == 0) {
        /* Touch the result so it is checked by ASan, and run the next
         * pipeline stage that consumes it directly. */
        course_compute_distances(&c);
    } else {
        /* The error message must be a NUL-terminated string. */
        if (strlen(err) >= sizeof err)
            abort();
    }
    course_free(&c);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    write_input(data, size);
    run_once(0);
    run_once(size ? 1 + (data[0] & 3) : 1);
    return 0;
}
