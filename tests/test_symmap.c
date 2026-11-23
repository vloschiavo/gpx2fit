/* test_symmap.c: tests for src/symmap.c (tasks 5.2 to 5.4). */
#define _POSIX_C_SOURCE 200809L /* mkstemp, strdup */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/names.h"
#include "../src/symmap.h"

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

/* ---- warning recorder ---------------------------------------------------- */

struct warn_log {
    int  n;
    int  line[8];
    char msg[8][160];
    char source[160];
};

static void log_warn(const char *source, int line, const char *msg, void *ctx)
{
    struct warn_log *w = ctx;

    if (w->n < 8) {
        w->line[w->n] = line;
        snprintf(w->msg[w->n], sizeof w->msg[w->n], "%s", msg);
    }
    snprintf(w->source, sizeof w->source, "%s", source);
    w->n++;
}

/* Parse text into a fresh map; the caller frees it. */
static void parse(struct symmap *m, struct warn_log *w, const char *text)
{
    symmap_init(m);
    memset(w, 0, sizeof *w);
    CHECK(symmap_parse(text, "test.map", m, log_warn, w) == SYMMAP_OK);
}

/* ---- 5.2: parser and matcher --------------------------------------------- */

static void test_case_insensitive(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w, "water = water\n");
    CHECK(w.n == 0);
    CHECK(symmap_lookup(&m, "water") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "WATER") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "WaTeR") == FIT_CP_WATER);
    symmap_free(&m);

    /* Keyword and type name are folded too. */
    parse(&m, &w, "CAMP = CampSite\n");
    CHECK(w.n == 0);
    CHECK(symmap_lookup(&m, "camp") == FIT_CP_CAMPSITE);
    CHECK(symmap_lookup(&m, "Base CAMP") == FIT_CP_CAMPSITE);
    symmap_free(&m);
}

static void test_substring_position(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w, "water = water\n");
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_WATER);               /* whole  */
    CHECK(symmap_lookup(&m, "Water Stop") == FIT_CP_WATER);          /* start  */
    CHECK(symmap_lookup(&m, "Big Water Fill") == FIT_CP_WATER);      /* middle */
    CHECK(symmap_lookup(&m, "Spring water") == FIT_CP_WATER);        /* end    */
    CHECK(symmap_lookup(&m, "water 2") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "Wate") == FIT_CP_GENERIC);              /* partial */
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_GENERIC);
    CHECK(symmap_lookup(&m, "") == FIT_CP_GENERIC);
    CHECK(symmap_lookup(&m, NULL) == FIT_CP_GENERIC);
    symmap_free(&m);

    /* Keywords with inner spaces keep them; non-ASCII bytes match exactly. */
    parse(&m, &w, "first aid = first_aid\ncaf\xC3\xA9 = food\n");
    CHECK(w.n == 0);
    CHECK(symmap_lookup(&m, "Trail First Aid Post") == FIT_CP_FIRST_AID);
    CHECK(symmap_lookup(&m, "firstaid") == FIT_CP_GENERIC);
    CHECK(symmap_lookup(&m, "Le Caf\xC3\xA9 du Pont") == FIT_CP_FOOD);
    symmap_free(&m);
}

static void test_find_vs_lookup(void)
{
    struct symmap m;
    struct warn_log w;
    uint8_t t = 99;

    parse(&m, &w, "waterfall = generic\n");
    /* A rule that maps to generic is still a match. */
    CHECK(symmap_find(&m, "Waterfall", &t) && t == FIT_CP_GENERIC);
    t = 99;
    CHECK(!symmap_find(&m, "Camp", &t) && t == FIT_CP_GENERIC);
    CHECK(!symmap_find(&m, NULL, &t));
    symmap_free(&m);

    symmap_init(&m); /* empty map matches nothing */
    CHECK(!symmap_find(&m, "Water", &t));
    symmap_free(&m);
}

static void test_first_match_wins(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w, "waterfall = overlook\nwater = water\n");
    CHECK(symmap_lookup(&m, "Waterfall") == FIT_CP_OVERLOOK);
    CHECK(symmap_lookup(&m, "Water Stop") == FIT_CP_WATER);
    symmap_free(&m);

    /* Reversed order: the general rule shadows the specific one. */
    parse(&m, &w, "water = water\nwaterfall = overlook\n");
    CHECK(symmap_lookup(&m, "Waterfall") == FIT_CP_WATER);
    symmap_free(&m);

    /* Two rules can match one name; file order decides, not name position. */
    parse(&m, &w, "camp = campsite\nwater = water\n");
    CHECK(symmap_lookup(&m, "Water at Camp") == FIT_CP_CAMPSITE);
    symmap_free(&m);
}

static void test_comments_and_blanks(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w,
          "# a comment\n"
          "\n"
          "   \t \n"
          "  water = water   # trailing comment\n"
          "#camp = campsite\n"
          "\tcaution\t=\tdanger\t\n"
          "# last comment");                      /* no final newline */
    CHECK(w.n == 0);
    CHECK(m.n == 2);
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "Caution") == FIT_CP_DANGER);
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_GENERIC);  /* commented out */
    symmap_free(&m);

    /* CRLF line endings, and a last rule without a newline. */
    parse(&m, &w, "water = water\r\n\r\ncamp = campsite");
    CHECK(w.n == 0);
    CHECK(m.n == 2);
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_CAMPSITE);
    CHECK(strcmp(m.rules[0].keyword, "water") == 0); /* no stray \r */
    symmap_free(&m);

    /* Empty text. */
    parse(&m, &w, "");
    CHECK(w.n == 0 && m.n == 0);
    symmap_free(&m);
}

static void test_unknown_type(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w, "water = water\nfoo = banana\ncamp = campsite\n");
    CHECK(w.n == 1);
    CHECK(w.line[0] == 2);
    CHECK(strstr(w.msg[0], "unknown type") && strstr(w.msg[0], "banana"));
    CHECK_STR(w.source, "test.map");
    CHECK(m.n == 2);                                       /* line skipped */
    CHECK(symmap_lookup(&m, "foo") == FIT_CP_GENERIC);
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_CAMPSITE);   /* parsing went on */
    symmap_free(&m);

    /* A type with inner spaces is a different (unknown) name. */
    parse(&m, &w, "x = first aid\n");
    CHECK(w.n == 1 && w.line[0] == 1 && m.n == 0);
    symmap_free(&m);

    /* A very long bogus type name is cut in the message, not overflowed. */
    parse(&m, &w, "x = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    CHECK(w.n == 1 && strlen(w.msg[0]) < sizeof w.msg[0]);
    symmap_free(&m);
}

static void test_malformed(void)
{
    struct symmap m;
    struct warn_log w;

    parse(&m, &w,
          "water = water\n"          /* 1 ok */
          "just words\n"             /* 2 no '=' */
          "= danger\n"               /* 3 empty keyword */
          "camp =\n"                 /* 4 empty type */
          "   =   \n"                /* 5 both empty */
          "caution = danger\n");     /* 6 ok */
    CHECK(w.n == 4);
    CHECK(w.line[0] == 2 && w.line[1] == 3 && w.line[2] == 4 && w.line[3] == 5);
    CHECK(strstr(w.msg[0], "malformed") != NULL);
    CHECK(m.n == 2);
    CHECK(symmap_lookup(&m, "Caution") == FIT_CP_DANGER);
    symmap_free(&m);

    /* Line numbers count blank and comment lines. */
    parse(&m, &w, "# c\n\n\nbad line\n");
    CHECK(w.n == 1 && w.line[0] == 4);
    symmap_free(&m);

    /* A NULL warn callback is allowed. */
    symmap_init(&m);
    CHECK(symmap_parse("bad\nwater = water\n", "x", &m, NULL, NULL) == SYMMAP_OK);
    CHECK(m.n == 1);
    symmap_free(&m);
}

static void test_type_names(void)
{
    uint8_t t;

    CHECK(symmap_type_from_name("water", &t) == 0 && t == 3);
    CHECK(symmap_type_from_name("DANGER", &t) == 0 && t == 5);
    CHECK(symmap_type_from_name("campsite", &t) == 0 && t == 27);
    CHECK(symmap_type_from_name("generic", &t) == 0 && t == 0);
    CHECK(symmap_type_from_name("first_aid", &t) == 0 && t == 9);
    CHECK(symmap_type_from_name("checkpoint", &t) == 0 && t == 35);
    CHECK(symmap_type_from_name("info", &t) == 0 && t == 53);   /* last in enum */
    CHECK(symmap_type_from_name("bogus", &t) == -1);
    CHECK(symmap_type_from_name("", &t) == -1);
    CHECK(symmap_type_from_name("wat", &t) == -1);              /* not a prefix match */
    CHECK(symmap_type_from_name("water ", &t) == -1);
    CHECK(symmap_type_from_name(NULL, &t) == -1);
    /* The enum constants agree with the table. */
    CHECK(symmap_type_from_name("shelter", &t) == 0 && t == FIT_CP_SHELTER);
    CHECK(symmap_type_from_name("toilet", &t) == 0 && t == FIT_CP_TOILET);
    CHECK(symmap_type_from_name("overlook", &t) == 0 && t == FIT_CP_OVERLOOK);
}

/* ---- 5.3: embedded defaults ---------------------------------------------- */

static void test_defaults(void)
{
    struct symmap m;
    struct warn_log w;
    uint8_t t;

    symmap_init(&m);
    memset(&w, 0, sizeof w);
    CHECK(symmap_load_defaults(&m, log_warn, &w) == SYMMAP_OK);
    CHECK(w.n == 0);              /* data/default.map itself parses cleanly */
    CHECK(m.n >= 3);

    CHECK(symmap_lookup(&m, "Water") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_CAMPSITE);
    CHECK(symmap_lookup(&m, "Caution") == FIT_CP_DANGER);
    CHECK(symmap_lookup(&m, "water 2") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "CAMP") == FIT_CP_CAMPSITE);
    /* "H2O" waypoint names (Sonora Pass export, phase 9) are water too. */
    CHECK(symmap_lookup(&m, "H2O Sardine Cr") == FIT_CP_WATER);
    CHECK(symmap_lookup(&m, "h2o") == FIT_CP_WATER);
    CHECK(!symmap_find(&m, "Totally Unknown Name", &t) && t == FIT_CP_GENERIC);

    /* The other names in the CalTopo sample have no rule on purpose. */
    CHECK(!symmap_find(&m, "Start", &t));
    CHECK(!symmap_find(&m, "Ranger Station", &t));
    symmap_free(&m);
}

/* ---- 5.4: --map layering ------------------------------------------------- */

/* Write content (len bytes) to a new temp file; returns a malloc'd path. */
static char *temp_file(const char *content, size_t len)
{
    char tmpl[] = "/tmp/test_symmap_XXXXXX";
    int fd = mkstemp(tmpl);
    FILE *f;

    if (fd < 0) {
        perror("mkstemp");
        exit(2);
    }
    f = fdopen(fd, "wb");
    fwrite(content, 1, len, f);
    fclose(f);
    return strdup(tmpl);
}

static void test_layering(void)
{
    struct symmap m;
    struct warn_log w;
    char err[256];
    char *path;
    uint8_t t;

    /* No user file: same as the defaults. */
    symmap_init(&m);
    memset(&w, 0, sizeof w);
    CHECK(symmap_load(&m, NULL, log_warn, &w, err, sizeof err) == SYMMAP_OK);
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_WATER);
    symmap_free(&m);

    /* A user rule overrides 'water'; an unrelated name still hits a default. */
    path = temp_file("water = food\n", 13);
    symmap_init(&m);
    memset(&w, 0, sizeof w);
    CHECK(symmap_load(&m, path, log_warn, &w, err, sizeof err) == SYMMAP_OK);
    CHECK(w.n == 0);
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_FOOD);          /* user wins */
    CHECK(symmap_lookup(&m, "Camp") == FIT_CP_CAMPSITE);       /* default */
    CHECK(symmap_lookup(&m, "Caution") == FIT_CP_DANGER);      /* default */
    CHECK(!symmap_find(&m, "Ranger Station", &t));             /* still none */
    symmap_free(&m);
    unlink(path);
    free(path);

    /* 'waterfall = generic' placed first beats the default 'water' rule. */
    path = temp_file("waterfall = generic\n", 20);
    symmap_init(&m);
    CHECK(symmap_load(&m, path, NULL, NULL, err, sizeof err) == SYMMAP_OK);
    CHECK(symmap_find(&m, "Upper Waterfall", &t) && t == FIT_CP_GENERIC);
    CHECK(symmap_lookup(&m, "Water Stop") == FIT_CP_WATER);    /* default */
    symmap_free(&m);
    unlink(path);
    free(path);

    /* Bad lines in the user file warn with that file's name and line. */
    path = temp_file("water = food\noops\n", 18);
    symmap_init(&m);
    memset(&w, 0, sizeof w);
    CHECK(symmap_load(&m, path, log_warn, &w, err, sizeof err) == SYMMAP_OK);
    CHECK(w.n == 1 && w.line[0] == 2);
    CHECK_STR(w.source, path);
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_FOOD);          /* rest still loads */
    symmap_free(&m);
    unlink(path);
    free(path);

    /* A user file with CRLF endings works. */
    path = temp_file("start = checkpoint\r\n", 20);
    symmap_init(&m);
    CHECK(symmap_load(&m, path, NULL, NULL, err, sizeof err) == SYMMAP_OK);
    CHECK(symmap_lookup(&m, "Start") == FIT_CP_CHECKPOINT);
    symmap_free(&m);
    unlink(path);
    free(path);

    /* An empty user file is fine: defaults only. */
    path = temp_file("", 0);
    symmap_init(&m);
    CHECK(symmap_load(&m, path, NULL, NULL, err, sizeof err) == SYMMAP_OK);
    CHECK(symmap_lookup(&m, "Water") == FIT_CP_WATER);
    symmap_free(&m);
    unlink(path);
    free(path);
}

static void test_load_errors(void)
{
    struct symmap m;
    char err[256];
    char *path;

    /* Missing file: error naming the file, nothing loaded. */
    symmap_init(&m);
    CHECK(symmap_load(&m, "/nonexistent/dir/none.map", NULL, NULL, err,
                      sizeof err) == SYMMAP_IOERR);
    CHECK(strstr(err, "/nonexistent/dir/none.map") != NULL);
    CHECK(m.n == 0);
    symmap_free(&m);

    /* A directory is not a map file. */
    symmap_init(&m);
    CHECK(symmap_load(&m, "/tmp", NULL, NULL, err, sizeof err) == SYMMAP_IOERR);
    CHECK(err[0] != '\0');
    symmap_free(&m);

    /* Embedded NUL byte. */
    path = temp_file("water = food\0camp = campsite\n", 29);
    symmap_init(&m);
    CHECK(symmap_load(&m, path, NULL, NULL, err, sizeof err) == SYMMAP_IOERR);
    CHECK(strstr(err, "NUL") != NULL);
    symmap_free(&m);
    unlink(path);
    free(path);

    /* Larger than a chunk, to exercise the read loop: 20000 comment bytes. */
    {
        size_t n = 20000;
        char *big = malloc(n + 20);

        memset(big, '#', n);
        memcpy(big + n, "\nwater = food\n", 14);
        path = temp_file(big, n + 14);
        free(big);
        symmap_init(&m);
        CHECK(symmap_load(&m, path, NULL, NULL, err, sizeof err) == SYMMAP_OK);
        CHECK(symmap_lookup(&m, "Water") == FIT_CP_FOOD);
        symmap_free(&m);
        unlink(path);
        free(path);
    }
}

/* ---- applying to a course ------------------------------------------------ */

static void test_apply_types(void)
{
    struct course c;
    struct symmap m;
    const char *names[] = { "Water", "Camp", "Foo", NULL,
                            "Water Stop Number Two", "Ranger Station",
                            "Caf\xC3\xA9 Water \xE2\x82\xAC\xE2\x82\xAC" };
    const uint8_t want_type[] = { FIT_CP_WATER, FIT_CP_CAMPSITE, FIT_CP_GENERIC,
                                  FIT_CP_GENERIC, FIT_CP_WATER, FIT_CP_GENERIC,
                                  FIT_CP_WATER };
    const size_t n = sizeof names / sizeof *names;

    memset(&c, 0, sizeof c);
    c.cp = calloc(n, sizeof *c.cp);
    c.n_cp = c.cap_cp = n;
    for (size_t i = 0; i < n; i++) {
        c.cp[i].name = names[i] ? strdup(names[i]) : NULL;
        c.cp[i].type = 77; /* must be overwritten */
    }

    symmap_init(&m);
    CHECK(symmap_load_defaults(&m, NULL, NULL) == SYMMAP_OK);

    /* Unmatched named points: "Foo" and "Ranger Station"; NULL is not counted. */
    CHECK(course_apply_types(&c, &m, true, NULL) == 2);
    for (size_t i = 0; i < n; i++)
        CHECK(c.cp[i].type == want_type[i]);

    CHECK_STR(c.cp[0].name, "Water");
    CHECK(c.cp[3].name == NULL);
    /* Matched on the full name, then cut to 15 bytes. */
    CHECK_STR(c.cp[4].name, "Water Stop Numb");
    CHECK_STR(c.cp[5].name, "Ranger Station");
    /* "Caf\xC3\xA9 Water " is 12 bytes + two 3-byte characters: only one fits. */
    CHECK_STR(c.cp[6].name, "Caf\xC3\xA9 Water \xE2\x82\xAC");
    for (size_t i = 0; i < n; i++)
        CHECK(!c.cp[i].name || strlen(c.cp[i].name) <= COURSE_POINT_NAME_MAX);

    /* Empty course is fine. */
    for (size_t i = 0; i < n; i++)
        free(c.cp[i].name);
    free(c.cp);
    memset(&c, 0, sizeof c);
    CHECK(course_apply_types(&c, &m, true, NULL) == 0);
    symmap_free(&m);
}

int main(void)
{
    test_case_insensitive();
    test_substring_position();
    test_find_vs_lookup();
    test_first_match_wins();
    test_comments_and_blanks();
    test_unknown_type();
    test_malformed();
    test_type_names();
    test_defaults();
    test_layering();
    test_load_errors();
    test_apply_types();

    if (failures) {
        fprintf(stderr, "test_symmap: %d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("test_symmap: all %d checks passed\n", checks);
    return 0;
}
