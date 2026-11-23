/*
 * fuzz_driver.c: standalone gcc driver for fuzz_gpx.c (task 10.2).
 *
 * Used when libFuzzer (clang) and AFL++ are not available. Not linked into
 * the libFuzzer build, which supplies its own main().
 *
 *   fuzz_driver [-t SECONDS] [-s SEED] [-o OUTDIR] [-m MAXLEN] PATH...
 *
 * Every PATH (file, or directory of files) is first replayed through
 * LLVMFuzzerTestOneInput. Without -t (or with -t 0) that is all: replay mode,
 * for reproducing a saved crash. With -t SECONDS > 0 the replayed files become
 * the seed corpus for a time-bounded, non-coverage-guided mutation loop:
 * stacked byte flips, inserts, deletes, duplications, splices with other
 * corpus entries, injected GPX/XML tokens (elements, entities, huge numbers,
 * NaN, invalid UTF-8, deep nesting, long names), plus fully synthetic GPX
 * documents from a small grammar.
 *
 * Build with -fsanitize=address,undefined -fno-sanitize-recover=undefined so
 * any finding aborts. On a sanitizer death, a fatal signal or a per-input
 * timeout (10 s) the current input is written to OUTDIR/crash-<n>.gpx and
 * the run exits non-zero. Reproduce with: fuzz_driver OUTDIR/crash-<n>.gpx
 * UBSan must run with UBSAN_OPTIONS=...:abort_on_error=1 (as `make fuzz`
 * does); otherwise gcc's runtime just exits 1 and nothing is saved.
 * LeakSanitizer checks only at process exit: a leak fails the run, but the
 * leaking input is not identified (replay the corpus to find it).
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#if defined(__has_include)
#  if __has_include(<sanitizer/common_interface_defs.h>)
#    include <sanitizer/common_interface_defs.h>
#    define HAVE_SAN_DEATH_CB 1
#  endif
#endif

#define TIMEOUT_S   10
#define MAX_CORPUS  4096

/* ---- current input, saved on death --------------------------------------- */

static const uint8_t *cur_data;
static size_t         cur_size;
static const char    *out_dir = "fuzz-out";

static volatile sig_atomic_t saved;

static void save_current(const char *why)
{
    char path[512];
    if (saved)
        return;
    saved = 1;
    int fd = -1;
    for (unsigned n = 0; n < 100000 && fd < 0; n++) {
        snprintf(path, sizeof path, "%s/crash-%u.gpx", out_dir, n);
        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd < 0 && errno != EEXIST)
            break;
    }
    if (fd < 0) {
        static const char m[] = "fuzz_driver: could not save crashing input\n";
        (void)!write(2, m, sizeof m - 1);
        return;
    }
    size_t off = 0;
    while (cur_data && off < cur_size) {
        ssize_t w = write(fd, cur_data + off, cur_size - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
    close(fd);
    /* snprintf is not async-signal-safe in theory; we are dying anyway. */
    char msg[640];
    int len = snprintf(msg, sizeof msg,
                       "fuzz_driver: %s; input (%zu bytes) saved to %s\n",
                       why, cur_size, path);
    if (len > 0)
        (void)!write(2, msg, (size_t)len < sizeof msg ? (size_t)len : sizeof msg - 1);
}

static volatile sig_atomic_t in_case;

#ifdef HAVE_SAN_DEATH_CB
static void on_san_death(void)
{
    if (in_case)
        save_current("sanitizer report");
}
#endif

static void on_signal(int sig)
{
    if (in_case)
        save_current(sig == SIGALRM ? "timeout" : "fatal signal");
    if (sig == SIGALRM)
        _exit(3);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void run_case(const uint8_t *d, size_t n)
{
    cur_data = d;
    cur_size = n;
    in_case = 1;
    alarm(TIMEOUT_S);
    LLVMFuzzerTestOneInput(d, n);
    alarm(0);
    in_case = 0;
}

/* ---- corpus --------------------------------------------------------------- */

struct buf {
    uint8_t *p;
    size_t   n;
};

static struct buf corpus[MAX_CORPUS];
static size_t     n_corpus;
static size_t     maxlen = 1u << 20; /* -m: largest mutant, bytes */

/* mkdir -p: create dir and any missing parents. */
static int mkdir_p(const char *dir)
{
    char p[4096];
    size_t n = strlen(dir);
    if (n == 0 || n >= sizeof p) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(p, dir, n + 1);
    for (size_t i = 1; i <= n; i++) {
        if (p[i] != '/' && p[i] != '\0')
            continue;
        char c = p[i];
        p[i] = '\0';
        if (mkdir(p, 0755) != 0 && errno != EEXIST)
            return -1;
        p[i] = c;
    }
    return 0;
}

static int read_file(const char *path, struct buf *b)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    size_t cap = 4096, n = 0;
    uint8_t *p = malloc(cap);
    if (!p) {
        fclose(f);
        return -1;
    }
    size_t r;
    while ((r = fread(p + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            uint8_t *q = realloc(p, cap *= 2);
            if (!q) {
                free(p);
                fclose(f);
                return -1;
            }
            p = q;
        }
    }
    fclose(f);
    b->p = p;
    b->n = n;
    return 0;
}

static void add_path(const char *path)
{
    struct stat sb;
    if (stat(path, &sb) != 0) {
        fprintf(stderr, "fuzz_driver: %s: %s\n", path, strerror(errno));
        exit(2);
    }
    if (S_ISDIR(sb.st_mode)) {
        DIR *d = opendir(path);
        struct dirent *e;
        if (!d)
            return;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            char sub[4096];
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            struct stat ss;
            if (stat(sub, &ss) == 0 && S_ISREG(ss.st_mode))
                add_path(sub);
        }
        closedir(d);
        return;
    }
    struct buf b;
    if (read_file(path, &b) != 0) {
        fprintf(stderr, "fuzz_driver: cannot read %s\n", path);
        exit(2);
    }
    fprintf(stderr, "replay %s (%zu bytes)\n", path, b.n);
    run_case(b.p, b.n);
    if (b.n > maxlen) {
        /* Replayed, but too big to mutate within -m: not used as a seed. */
        fprintf(stderr, "fuzz_driver: warning: %s (%zu bytes) exceeds "
                "maxlen %zu; not used as a seed\n", path, b.n, maxlen);
        free(b.p);
    } else if (n_corpus < MAX_CORPUS)
        corpus[n_corpus++] = b;
    else
        free(b.p);
}

/* ---- PRNG (xorshift64*) ---------------------------------------------------- */

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint64_t rnd(void)
{
    uint64_t x = rng_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    rng_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

static size_t rndn(size_t n) { return n ? (size_t)(rnd() % n) : 0; }

/* ---- growable output buffer ----------------------------------------------- */

struct out {
    uint8_t *p;
    size_t   n, cap, max;
};

static void out_reserve(struct out *o, size_t extra)
{
    if (o->n + extra <= o->cap)
        return;
    size_t cap = o->cap ? o->cap : 1024;
    while (cap < o->n + extra)
        cap *= 2;
    uint8_t *q = realloc(o->p, cap);
    if (!q) {
        perror("realloc");
        exit(2);
    }
    o->p = q;
    o->cap = cap;
}

static void out_put(struct out *o, const void *s, size_t n)
{
    if (n == 0 || o->n + n > o->max)
        return;
    out_reserve(o, n);
    memcpy(o->p + o->n, s, n);
    o->n += n;
}

static void out_str(struct out *o, const char *s) { out_put(o, s, strlen(s)); }

static void out_insert(struct out *o, size_t at, const void *s, size_t n)
{
    if (n == 0 || o->n + n > o->max || at > o->n)
        return;
    out_reserve(o, n);
    memmove(o->p + at + n, o->p + at, o->n - at);
    memcpy(o->p + at, s, n);
    o->n += n;
}

/* ---- tokens --------------------------------------------------------------- */

static const char *const tokens[] = {
    "<gpx>", "</gpx>",
    "<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" version=\"1.1\">",
    "<gpx xmlns=\"http://www.topografix.com/GPX/1/0\" version=\"1.0\">",
    "<g:gpx xmlns:g=\"http://www.topografix.com/GPX/1/1\">",
    "<trk>", "</trk>", "<trkseg>", "</trkseg>",
    "<trkpt lat=\"", "<trkpt lon=\"", "\" lon=\"", "\" lat=\"", "\">", "\"/>",
    "</trkpt>", "<trkpt lat=\"1\" lon=\"2\"/>",
    "<trkpt lat=\"37.5\" lon=\"-119.3\"><ele>2500</ele></trkpt>",
    "<wpt", "<wpt lat=\"37.1\" lon=\"-119.2\">", "</wpt>",
    "<wpt lat=\"0\" lon=\"0\"><name>x</name></wpt>",
    "<name>", "</name>", "<ele>", "</ele>", "<sym>", "</sym>", "<desc>",
    "<extensions><gpxx:x xmlns:gpxx=\"urn:garmin\"><name>n</name></gpxx:x></extensions>",
    "<rte>", "<rtept lat=\"1\" lon=\"1\"/>", "</rte>",
    "&amp;", "&lt;", "&#0;", "&#x10FFFF;", "&#xD800;", "&#65;", "&bogus;",
    "<![CDATA[", "]]>", "<!--", "-->", "<?xml version=\"1.0\"?>",
    "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>",
    "<?xml version=\"1.0\" encoding=\"UTF-16\"?>",
    "<!DOCTYPE gpx [<!ENTITY e \"<trkpt lat='1' lon='1'/>\">]>", "&e;",
    "<!DOCTYPE a [<!ENTITY a \"aaaaaaaaaa\"><!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;\">]>",
    "1e308", "-1e308", "1e309", "4.9e-324", "NaN", "nan", "-nan", "inf",
    "-Infinity", "0x1p3", "90", "-90", "90.0000001", "180", "-180.0000001",
    "999999999999999999999999999999", "-0", " 12 ", "\t1\n", "1,5", "",
    "00000000000000000000000000000000000000000000001",
    "\xff\xfe", "\xef\xbb\xbf", "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80",
    "\xe2\x82", "\x80", "\x00", "\xc3\xa9", "\xf0\x9f\x8f\x94",
    " xmlns=\"http://www.topografix.com/GPX/1/1\"", " xmlns=\"urn:other\"",
    " xmlns:a=\"http://www.topografix.com/GPX/1/1\"", "a:", "{x}", " ",
};
#define N_TOKENS (sizeof tokens / sizeof tokens[0])

/* Insert something big: deep nesting, a long name, many points. */
static void insert_big(struct out *o, size_t at)
{
    struct out t = { .max = o->max };
    size_t reps = 1 + rndn(rnd() & 1 ? 64 : 20000);
    switch (rndn(5)) {
    case 0:
        for (size_t i = 0; i < reps; i++)
            out_str(&t, rnd() & 1 ? "<trk>" : "<x>");
        break;
    case 1:
        out_str(&t, "<name>");
        for (size_t i = 0; i < reps; i++)
            out_str(&t, rnd() & 1 ? "\xc3\xa9" : "n");
        out_str(&t, "</name>");
        break;
    case 2:
        for (size_t i = 0; i < reps; i++) {
            char b[96];
            snprintf(b, sizeof b, "<trkpt lat=\"%d.%u\" lon=\"%d.%u\"/>",
                     (int)rndn(181) - 90, (unsigned)rndn(100000),
                     (int)rndn(361) - 180, (unsigned)rndn(100000));
            out_str(&t, b);
        }
        break;
    case 3:
        for (size_t i = 0; i < reps; i++)
            out_str(&t, "</trkseg><trkseg>");
        break;
    default:
        for (size_t i = 0; i < reps; i++)
            out_str(&t, tokens[rndn(N_TOKENS)]);
        break;
    }
    out_insert(o, at, t.p, t.n);
    free(t.p);
}

/* ---- grammar-based generator --------------------------------------------- */

static void gen_num(struct out *o, double range)
{
    char b[64];
    if (rndn(12) == 0) {
        out_str(o, tokens[rndn(N_TOKENS)]);
        return;
    }
    double v = ((double)rndn(2000001) / 1000000.0 - 1.0) * range;
    snprintf(b, sizeof b, "%.*f", (int)rndn(10), v);
    out_str(o, b);
}

static void gen_name(struct out *o)
{
    static const char *const names[] = {
        "Water", "Camp", "Trailhead", "Summit", "Pass", "Lake",
        "Junction", "\xc3\x89t\xc3\xa9", "a&amp;b", "", "   ", "x<!--c-->y",
        "<![CDATA[cd]]>", "\xf0\x9f\x8f\x94 peak",
    };
    out_str(o, "<name>");
    size_t k = 1 + rndn(3);
    for (size_t i = 0; i < k; i++)
        out_str(o, names[rndn(sizeof names / sizeof names[0])]);
    out_str(o, "</name>");
}

static void gen_pt(struct out *o, const char *tag)
{
    out_str(o, "<");
    out_str(o, tag);
    if (rndn(2)) {
        out_str(o, " lat=\""); gen_num(o, 95);
        out_str(o, "\" lon=\""); gen_num(o, 185);
    } else {
        out_str(o, " lon=\""); gen_num(o, 185);
        out_str(o, "\" lat=\""); gen_num(o, 95);
    }
    out_str(o, "\"");
    if (rndn(3) == 0) {
        out_str(o, "/>");
        return;
    }
    out_str(o, ">");
    if (rndn(2)) { out_str(o, "<ele>"); gen_num(o, 9000); out_str(o, "</ele>"); }
    if (rndn(2)) gen_name(o);
    if (rndn(4) == 0) out_str(o, "<sym>Flag</sym><time>2020</time>");
    out_str(o, "</");
    out_str(o, tag);
    out_str(o, ">");
}

static void gen_doc(struct out *o)
{
    static const char *const heads[] = {
        "<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" version=\"1.1\">",
        "<gpx xmlns=\"http://www.topografix.com/GPX/1/0\" version=\"1.0\">",
        "<gpx>",
        "<g:gpx xmlns:g=\"http://www.topografix.com/GPX/1/1\">",
    };
    size_t h = rndn(4);
    if (rndn(2))
        out_str(o, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    out_str(o, heads[h]);
    size_t items = rndn(8);
    for (size_t i = 0; i < items; i++) {
        if (rndn(2)) {
            gen_pt(o, h == 3 ? "g:wpt" : "wpt");
            continue;
        }
        out_str(o, h == 3 ? "<g:trk>" : "<trk>");
        if (rndn(2)) gen_name(o);
        size_t segs = rndn(4);
        for (size_t s = 0; s < segs; s++) {
            out_str(o, h == 3 ? "<g:trkseg>" : "<trkseg>");
            size_t pts = rndn(rndn(4) == 0 ? 500 : 6);
            for (size_t p = 0; p < pts; p++)
                gen_pt(o, h == 3 ? "g:trkpt" : "trkpt");
            out_str(o, h == 3 ? "</g:trkseg>" : "</trkseg>");
        }
        out_str(o, h == 3 ? "</g:trk>" : "</trk>");
    }
    out_str(o, h == 3 ? "</g:gpx>" : "</gpx>");
}

/* ---- mutation -------------------------------------------------------------- */

static void mutate(struct out *o)
{
    size_t k = 1 + rndn(rndn(4) == 0 ? 32 : 6);
    for (size_t i = 0; i < k; i++) {
        size_t at = rndn(o->n + 1);
        switch (rndn(10)) {
        case 0: /* bit flip */
            if (o->n) o->p[rndn(o->n)] ^= (uint8_t)(1u << rndn(8));
            break;
        case 1: /* random byte */
            if (o->n) o->p[rndn(o->n)] = (uint8_t)rnd();
            break;
        case 2: { /* delete a range */
            if (!o->n) break;
            size_t s = rndn(o->n), len = 1 + rndn(rndn(2) ? 8 : o->n - s);
            if (len > o->n - s) len = o->n - s;
            memmove(o->p + s, o->p + s + len, o->n - s - len);
            o->n -= len;
            break;
        }
        case 3: { /* insert random bytes */
            uint8_t b[16];
            size_t len = 1 + rndn(sizeof b);
            for (size_t j = 0; j < len; j++) b[j] = (uint8_t)rnd();
            out_insert(o, at, b, len);
            break;
        }
        case 4: case 5: { /* insert a token */
            const char *t = tokens[rndn(N_TOKENS)];
            size_t len = strlen(t);
            if (len == 0) { /* "" and "\x00" tokens: a literal NUL byte */
                uint8_t z = 0;
                out_insert(o, at, &z, 1);
            } else {
                out_insert(o, at, t, len);
            }
            break;
        }
        case 6: { /* duplicate a range of itself */
            if (!o->n) break;
            size_t s = rndn(o->n), len = 1 + rndn(o->n - s);
            if (len > 4096) len = 4096;
            uint8_t *tmp = malloc(len);
            if (!tmp) break;
            memcpy(tmp, o->p + s, len);
            out_insert(o, at, tmp, len);
            free(tmp);
            break;
        }
        case 7: { /* splice from another corpus entry */
            if (!n_corpus) break;
            const struct buf *b = &corpus[rndn(n_corpus)];
            if (!b->n) break;
            size_t s = rndn(b->n), len = 1 + rndn(b->n - s);
            if (rndn(2)) { /* replace the tail */
                o->n = at;
            }
            out_insert(o, at, b->p + s, len);
            break;
        }
        case 8: /* something big */
            if (rndn(4) == 0) insert_big(o, at);
            break;
        default: { /* truncate */
            if (rndn(4) == 0) o->n = at;
            break;
        }
        }
    }
}

/* ---- main ------------------------------------------------------------------ */

static void usage(void)
{
    fprintf(stderr,
            "usage: fuzz_driver [-t SECONDS] [-s SEED] [-o OUTDIR] [-m MAXLEN] PATH...\n");
    exit(2);
}

int main(int argc, char **argv)
{
    long seconds = 0;
    uint64_t seed = (uint64_t)time(NULL);

    int opt;

    while ((opt = getopt(argc, argv, "t:s:o:m:")) != -1) {
        switch (opt) {
        case 't': seconds = strtol(optarg, NULL, 10); break;
        case 's': seed = strtoull(optarg, NULL, 10); break;
        case 'o': out_dir = optarg; break;
        case 'm': maxlen = strtoul(optarg, NULL, 10); break;
        default:  usage();
        }
    }
    if (optind >= argc && seconds <= 0)
        usage();

    if (mkdir_p(out_dir) != 0) {
        fprintf(stderr, "fuzz_driver: cannot create %s: %s\n", out_dir,
                strerror(errno));
        return 2;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
#ifdef HAVE_SAN_DEATH_CB
    __sanitizer_set_death_callback(on_san_death);
#endif

    for (int i = optind; i < argc; i++)
        add_path(argv[i]);
    fprintf(stderr, "replayed %zu input(s)\n", n_corpus);
    if (seconds <= 0)
        return 0;

    rng_state = seed ? seed : 1;
    fprintf(stderr, "fuzzing for %ld s, seed %llu, maxlen %zu\n",
            seconds, (unsigned long long)seed, maxlen);

    time_t start = time(NULL), last = start;
    unsigned long long execs = 0;
    struct out o = { .max = maxlen };
    for (;;) {
        o.n = 0;
        if (n_corpus == 0 || rndn(8) == 0) {
            gen_doc(&o);
            if (rndn(2))
                mutate(&o);
        } else {
            const struct buf *b = &corpus[rndn(n_corpus)];
            out_put(&o, b->p, b->n);
            mutate(&o);
        }
        run_case(o.p ? o.p : (const uint8_t *)"", o.n);
        execs++;

        /* Keep a few mutants as new seeds so mutations can stack over time
         * (no coverage feedback, so this is random sampling). */
        if (rndn(256) == 0 && o.n) {
            struct buf nb = { malloc(o.n), o.n };
            if (nb.p) {
                memcpy(nb.p, o.p, o.n);
                if (n_corpus < MAX_CORPUS) {
                    corpus[n_corpus++] = nb;
                } else {
                    size_t j = rndn(n_corpus);
                    free(corpus[j].p);
                    corpus[j] = nb;
                }
            }
        }

        if ((execs & 255) == 0) {
            time_t now = time(NULL);
            if (now - last >= 30) {
                fprintf(stderr, "  %lld s: %llu execs (%.0f/s), corpus %zu\n",
                        (long long)(now - start), execs,
                        (double)execs / (double)(now - start ? now - start : 1),
                        n_corpus);
                last = now;
            }
            if (now - start >= seconds)
                break;
        }
    }
    time_t el = time(NULL) - start;
    fprintf(stderr, "done: %llu execs in %lld s, no findings\n",
            execs, (long long)el);
    free(o.p);
    for (size_t i = 0; i < n_corpus; i++)
        free(corpus[i].p);
    return 0;
}
