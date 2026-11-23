/*
 * test_fit.c: FIT encoder tests (phase 7).
 *
 * Run from the project root (make test does). It reads
 * tests/data/reference_course.fit when present, and writes its fixture files
 * to $FIT_TEST_OUT (default build-test/) for tests/test_fit_decode.sh, which
 * decodes them with the Garmin SDK tool.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fit_write.h"
#include "names.h"
#include "symmap.h" /* FIT_CP_* enum values only */

static int fails;

#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #c);   \
            fails++;                                                          \
        }                                                                     \
    } while (0)

/* ---- a small FIT reader (independent of the writer, handles both
 *      byte orders) ------------------------------------------------------- */

struct fdef { uint8_t num, size, base; };
struct mdef {
    uint16_t     global;
    int          big;
    int          nf;
    struct fdef  f[64];
    size_t       size;
};
struct scan {
    struct mdef defs[256]; /* every definition message, in file order */
    int         n_defs;
    uint16_t    seq[4096]; /* global number of every data message */
    size_t      n_seq;
};
typedef void (*scan_cb)(const struct mdef *d, const uint8_t *p, void *ctx);

static uint64_t rd(const uint8_t *p, size_t n, int big)
{
    uint64_t v = 0;

    for (size_t i = 0; i < n; i++)
        v |= (uint64_t)p[big ? n - 1 - i : i] << (8 * i);
    return v;
}

/* 0 if the file is structurally valid (header, sizes, both CRCs). */
static int scan_fit(const uint8_t *b, size_t len, struct scan *s, scan_cb cb,
                    void *ctx)
{
    struct mdef local[16];
    int have[16] = { 0 };
    size_t hs, pos, end;

    memset(s, 0, sizeof *s);
    memset(local, 0, sizeof local);
    if (len < 16) return -1;
    hs = b[0];
    if (hs != 14 && hs != 12) return -2;
    if (memcmp(b + 8, ".FIT", 4) != 0) return -3;
    end = hs + (size_t)rd(b + 4, 4, 0);
    if (end + 2 != len) return -4;
    if (hs == 14 && rd(b + 12, 2, 0) != fit_crc16(0, b, 12)) return -5;
    if (rd(b + len - 2, 2, 0) != fit_crc16(0, b, len - 2)) return -6;
    pos = hs;
    while (pos < end) {
        uint8_t h = b[pos++], lt = h & 0x0F;

        if (h & 0x80) return -7;
        if (h & 0x40) {
            struct mdef *d = &local[lt];

            if ((h & 0x20) || pos + 5 > end) return -8;
            memset(d, 0, sizeof *d);
            d->big = b[pos + 1];
            d->global = (uint16_t)rd(b + pos + 2, 2, d->big);
            d->nf = b[pos + 4];
            pos += 5;
            if (d->nf > 64 || pos + 3 * (size_t)d->nf > end) return -9;
            for (int i = 0; i < d->nf; i++, pos += 3) {
                d->f[i].num = b[pos];
                d->f[i].size = b[pos + 1];
                d->f[i].base = b[pos + 2];
                d->size += b[pos + 1];
            }
            have[lt] = 1;
            if (s->n_defs < 256) s->defs[s->n_defs++] = *d;
        } else {
            const struct mdef *d = &local[lt];

            if (!have[lt] || pos + d->size > end) return -10;
            if (s->n_seq < 4096) s->seq[s->n_seq++] = d->global;
            if (cb) cb(d, b + pos, ctx);
            pos += d->size;
        }
    }
    return 0;
}

/* Pointer to field `num` in a data message, or NULL. */
static const uint8_t *fld(const struct mdef *d, const uint8_t *p, uint8_t num,
                          size_t *size)
{
    size_t off = 0;

    for (int i = 0; i < d->nf; i++) {
        if (d->f[i].num == num) {
            if (size) *size = d->f[i].size;
            return p + off;
        }
        off += d->f[i].size;
    }
    return NULL;
}

/* Integer value of a field; UINT64_MAX if the field is absent. */
static uint64_t fv(const struct mdef *d, const uint8_t *p, uint8_t num)
{
    size_t sz;
    const uint8_t *q = fld(d, p, num, &sz);

    return q ? rd(q, sz, d->big) : UINT64_MAX;
}

/* Sequence of messages as letters. */
static void seq_str(const struct scan *s, char *out, size_t n)
{
    size_t i;

    for (i = 0; i < s->n_seq && i + 1 < n; i++) {
        switch (s->seq[i]) {
        case FIT_MSG_FILE_ID:      out[i] = 'I'; break;
        case FIT_MSG_COURSE:       out[i] = 'C'; break;
        case FIT_MSG_LAP:          out[i] = 'L'; break;
        case FIT_MSG_EVENT:        out[i] = 'E'; break;
        case FIT_MSG_RECORD:       out[i] = 'r'; break;
        case FIT_MSG_COURSE_POINT: out[i] = 'p'; break;
        case 49:                   out[i] = 'F'; break; /* file_creator */
        default:                   out[i] = '?'; break;
        }
    }
    out[i] = '\0';
}

/* ---- decoded view of a file we wrote ------------------------------------ */

struct drec { int32_t lat, lon; uint32_t ts, dist; uint16_t alt; };
struct dcp {
    uint16_t idx; uint32_t ts; uint8_t type; int32_t lat, lon; uint32_t dist;
    char name[256];
};
struct dec {
    int      n_fileid;
    uint8_t  ftype;
    uint16_t manu, prod;
    uint32_t tc;
    uint8_t  sport, subsport;
    char     cname[256];
    size_t   cname_size;
    int      n_lap;
    struct {
        uint32_t start, ts, elapsed, timer, dist;
        int32_t  slat, slon, elat, elon, swlat, swlon, nelat, nelon;
        uint16_t asc, desc, avg, idx;
    } lap;
    struct drec rec[64]; int n_rec;
    struct dcp  cp[16];  int n_cp;
    uint32_t ev_ts[4]; uint8_t ev_type[4]; int n_ev;
};

static void dec_cb(const struct mdef *d, const uint8_t *p, void *ctx)
{
    struct dec *o = ctx;
    size_t sz;
    const uint8_t *q;

    switch (d->global) {
    case FIT_MSG_FILE_ID:
        o->n_fileid++;
        o->ftype = (uint8_t)fv(d, p, 0);
        o->manu = (uint16_t)fv(d, p, 1);
        o->prod = (uint16_t)fv(d, p, 2);
        o->tc = (uint32_t)fv(d, p, 4);
        break;
    case FIT_MSG_COURSE:
        o->sport = (uint8_t)fv(d, p, 4);
        o->subsport = (uint8_t)fv(d, p, 7);
        q = fld(d, p, 5, &sz);
        if (q && sz < sizeof o->cname) {
            memcpy(o->cname, q, sz);
            o->cname_size = sz;
        }
        break;
    case FIT_MSG_LAP:
        o->n_lap++;
        o->lap.start = (uint32_t)fv(d, p, 2);
        o->lap.ts = (uint32_t)fv(d, p, 253);
        o->lap.slat = (int32_t)fv(d, p, 3);
        o->lap.slon = (int32_t)fv(d, p, 4);
        o->lap.elat = (int32_t)fv(d, p, 5);
        o->lap.elon = (int32_t)fv(d, p, 6);
        o->lap.asc = (uint16_t)fv(d, p, 21);
        o->lap.desc = (uint16_t)fv(d, p, 22);
        o->lap.swlat = (int32_t)fv(d, p, 29);
        o->lap.swlon = (int32_t)fv(d, p, 30);
        o->lap.nelat = (int32_t)fv(d, p, 27);
        o->lap.nelon = (int32_t)fv(d, p, 28);
        o->lap.avg = (uint16_t)fv(d, p, 13);
        o->lap.elapsed = (uint32_t)fv(d, p, 7);
        o->lap.timer = (uint32_t)fv(d, p, 8);
        o->lap.dist = (uint32_t)fv(d, p, 9);
        o->lap.idx = (uint16_t)fv(d, p, 254);
        break;
    case FIT_MSG_EVENT:
        if (o->n_ev < 4) {
            o->ev_ts[o->n_ev] = (uint32_t)fv(d, p, 253);
            o->ev_type[o->n_ev] = (uint8_t)fv(d, p, 1);
            o->n_ev++;
        }
        break;
    case FIT_MSG_RECORD:
        if (o->n_rec < 64) {
            struct drec *r = &o->rec[o->n_rec++];

            r->lat = (int32_t)fv(d, p, 0);
            r->lon = (int32_t)fv(d, p, 1);
            r->ts = (uint32_t)fv(d, p, 253);
            r->dist = (uint32_t)fv(d, p, 5);
            r->alt = (uint16_t)fv(d, p, 2);
        }
        break;
    case FIT_MSG_COURSE_POINT:
        if (o->n_cp < 16) {
            struct dcp *c = &o->cp[o->n_cp++];

            c->idx = (uint16_t)fv(d, p, 254);
            c->ts = (uint32_t)fv(d, p, 1);
            c->type = (uint8_t)fv(d, p, 5);
            c->lat = (int32_t)fv(d, p, 2);
            c->lon = (int32_t)fv(d, p, 3);
            c->dist = (uint32_t)fv(d, p, 4);
            q = fld(d, p, 6, &sz);
            if (q && sz < sizeof c->name) {
                memcpy(c->name, q, sz);
                c->name[sz] = '\0';
            }
        }
        break;
    default:
        break;
    }
}

/* ---- helpers ------------------------------------------------------------ */

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static int bytes_eq(const struct fit_buf *b, const uint8_t *want, size_t n)
{
    return !b->failed && b->len == n && memcmp(b->data, want, n) == 0;
}

/* ---- 7.1 CRC ------------------------------------------------------------ */

static void test_crc(void)
{
    const char *s = "123456789";
    uint16_t c;

    CHECK(fit_crc16(0, "", 0) == 0);
    CHECK(fit_crc16(0, s, 9) == 0xBB3D); /* CRC-16/ARC check value */
    c = fit_crc16(0, s, 4);
    CHECK(fit_crc16(c, s + 4, 5) == 0xBB3D); /* incremental */
    CHECK(fit_crc16(0, "\x01", 1) == 0xC0C1);
    {
        /* data followed by its own CRC (little-endian) gives 0 */
        uint8_t t[11];
        memcpy(t, s, 9);
        t[9] = 0x3D;
        t[10] = 0xBB;
        CHECK(fit_crc16(0, t, 11) == 0);
    }
}

/* CRCs stored in the Garmin reference file (header 0x98E4, file 0xD1FC). */
static void test_crc_reference(void)
{
    size_t n;
    uint8_t *d = slurp("tests/data/reference_course.fit", &n);

    if (!d) {
        printf("  (skip: tests/data/reference_course.fit not found)\n");
        return;
    }
    CHECK(fit_crc16(0, d, 12) == 0x98E4);
    CHECK(fit_crc16(0, d, n - 2) == 0xD1FC);
    CHECK(fit_crc16(0, d, n) == 0);
    free(d);
}

/* ---- 7.2 Buffer --------------------------------------------------------- */

static void test_buf(void)
{
    struct fit_buf b;
    static const uint8_t want[] = { 0xAB, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12,
                                    'h', 'i' };

    fit_buf_init(&b);
    fit_put_u8(&b, 0xAB);
    fit_put_u16(&b, 0x1234);
    fit_put_u32(&b, 0x12345678);
    fit_put_bytes(&b, "hi", 2);
    CHECK(bytes_eq(&b, want, sizeof want));

    /* growth across many reallocations */
    for (int i = 0; i < 100000; i++)
        fit_put_u8(&b, (uint8_t)i);
    CHECK(!b.failed && b.len == sizeof want + 100000);
    CHECK(b.data[sizeof want + 99999] == (uint8_t)99999);
    fit_buf_free(&b);
    CHECK(b.len == 0 && b.data == NULL);
}

static void test_string_field(void)
{
    struct fit_buf b;
    static const uint8_t pad[] = { 'a', 'b', 0, 0, 0 };
    static const uint8_t cut[] = { 'a', 'b', 'c', 0 };
    static const uint8_t empty[] = { 0, 0, 0 };
    static const uint8_t mb[] = { 'a', 'a', 'a', 'a', 0xC3, 0xA9, 0 }; /* "aaaaé" */
    static const uint8_t mb_cut[] = { 'a', 'a', 'a', 'a', 0, 0 };

    fit_buf_init(&b);
    fit_put_string_field(&b, "ab", 5);
    CHECK(bytes_eq(&b, pad, sizeof pad));
    fit_buf_free(&b);

    fit_put_string_field(&b, "abcdef", 4); /* exact fit minus NUL */
    CHECK(bytes_eq(&b, cut, sizeof cut));
    fit_buf_free(&b);

    fit_put_string_field(&b, NULL, 3);
    CHECK(bytes_eq(&b, empty, sizeof empty));
    fit_buf_free(&b);

    fit_put_string_field(&b, "aaaa\xC3\xA9", 7); /* 6 bytes + NUL: whole char fits */
    CHECK(bytes_eq(&b, mb, sizeof mb));
    fit_buf_free(&b);

    fit_put_string_field(&b, "aaaa\xC3\xA9", 6); /* would cut the char in half */
    CHECK(bytes_eq(&b, mb_cut, sizeof mb_cut));
    fit_buf_free(&b);

    fit_put_string_field(&b, "abc", 0); /* size 0 writes nothing */
    CHECK(b.len == 0 && !b.failed);
    fit_buf_free(&b);
}

static void test_save(const char *dir)
{
    struct fit_buf b;
    char path[512];
    size_t n;
    uint8_t *back;

    snprintf(path, sizeof path, "%s/test_fit_save.bin", dir);
    fit_buf_init(&b);
    fit_put_u32(&b, 0xDEADBEEF);
    CHECK(fit_buf_save(&b, path) == FIT_OK);
    back = slurp(path, &n);
    CHECK(back && n == 4 && memcmp(back, "\xEF\xBE\xAD\xDE", 4) == 0);
    free(back);
    remove(path);
    CHECK(fit_buf_save(&b, "/nonexistent-dir/x.fit") == FIT_IOERR);
    fit_buf_free(&b);
}

/* ---- 7.3 Header and footer ---------------------------------------------- */

static void test_header_footer(void)
{
    struct fit_buf b;
    struct scan s;
    /* No messages: header, data size 0, header CRC, file CRC (0, because the
     * data ends with its own CRC... the whole file CRC of header+CRC is 0). */
    static const uint8_t empty[] = { 0x0E, 0x10, 0xE4, 0x52, 0, 0, 0, 0,
                                     '.', 'F', 'I', 'T', 0xC3, 0x65, 0, 0 };

    fit_buf_init(&b);
    fit_begin(&b);
    CHECK(b.len == FIT_HEADER_SIZE);
    CHECK(fit_finish(&b) == FIT_OK);
    CHECK(bytes_eq(&b, empty, sizeof empty));
    CHECK(scan_fit(b.data, b.len, &s, NULL, NULL) == 0);
    fit_buf_free(&b);

    /* with three data bytes: size patched, both CRCs valid */
    fit_begin(&b);
    fit_put_u8(&b, 1);
    fit_put_u8(&b, 2);
    fit_put_u8(&b, 3);
    CHECK(fit_finish(&b) == FIT_OK);
    CHECK(b.len == FIT_HEADER_SIZE + 3 + FIT_CRC_SIZE);
    CHECK(b.data[4] == 3 && b.data[5] == 0 && b.data[6] == 0 && b.data[7] == 0);
    CHECK(fit_crc16(0, b.data, 12) == (uint16_t)(b.data[12] | b.data[13] << 8));
    CHECK(fit_crc16(0, b.data, b.len) == 0);
    fit_buf_free(&b);

    /* finish without begin */
    fit_put_u8(&b, 1);
    CHECK(fit_finish(&b) == FIT_TOOBIG);
    fit_buf_free(&b);
}

/* ---- 7.4 Definition messages -------------------------------------------- */

static void test_definition(void)
{
    struct fit_buf b;
    static const struct fit_field_def f[] = {
        { 0, 1, FIT_BASE_ENUM }, { 1, 2, FIT_BASE_UINT16 },
        { 2, 2, FIT_BASE_UINT16 }, { 4, 4, FIT_BASE_UINT32 },
    };
    static const uint8_t want[] = {
        0x40, 0x00, 0x00, 0x00, 0x00, 0x04,   /* def, local 0, LE, msg 0, 4 */
        0x00, 0x01, 0x00, 0x01, 0x02, 0x84, 0x02, 0x02, 0x84, 0x04, 0x04, 0x86
    };

    fit_buf_init(&b);
    fit_put_definition(&b, FIT_LOCAL_FILE_ID, FIT_MSG_FILE_ID, f, 4);
    CHECK(bytes_eq(&b, want, sizeof want));
    fit_buf_free(&b);

    /* local type 5, global 32 is little-endian 0x20 0x00 */
    fit_put_definition(&b, 5, 32, f, 1);
    CHECK(b.data[0] == 0x45 && b.data[3] == 0x20 && b.data[4] == 0x00);
    fit_buf_free(&b);
}

/* Every field we define must exist, with the same base type (and size, except
 * for strings), in the reference file's definition of the same message. */
static int def_in_reference(const struct mdef *ours, const struct scan *ref)
{
    for (int r = 0; r < ref->n_defs; r++) {
        const struct mdef *rd_ = &ref->defs[r];
        int all = 1;

        if (rd_->global != ours->global)
            continue;
        for (int i = 0; i < ours->nf && all; i++) {
            int found = 0;

            for (int j = 0; j < rd_->nf; j++) {
                if (rd_->f[j].num == ours->f[i].num &&
                    rd_->f[j].base == ours->f[i].base &&
                    (ours->f[i].base == FIT_BASE_STRING ||
                     rd_->f[j].size == ours->f[i].size))
                    found = 1;
            }
            if (!found) {
                fprintf(stderr, "  message %u field %u not in reference\n",
                        ours->global, ours->f[i].num);
                all = 0;
            }
        }
        return all;
    }
    fprintf(stderr, "  message %u not in reference\n", ours->global);
    return 0;
}

/* ---- conversions (7.7) --------------------------------------------------- */

static void test_conversions(void)
{
    CHECK(fit_semicircles(0.0) == 0);
    CHECK(fit_semicircles(90.0) == 1073741824);
    CHECK(fit_semicircles(-90.0) == -1073741824);
    CHECK(fit_semicircles(-180.0) == INT32_MIN);
    CHECK(fit_semicircles(180.0) == 0x7FFFFFFE); /* not the invalid value */
    CHECK(fit_semicircles(NAN) == FIT_INVALID_SINT32);
    CHECK(fit_semicircles(INFINITY) == FIT_INVALID_SINT32);
    CHECK(fit_semicircles(-1000.0) == INT32_MIN);
    /* Round trips of two reference values (semicircles -> degrees -> back). */
    CHECK(fit_semicircles(464950366.0 * 180.0 / 2147483648.0) == 464950366);
    CHECK(fit_semicircles(-1443050446.0 * 180.0 / 2147483648.0) == -1443050446);
    CHECK(fit_semicircles(1.0 / 2147483648.0 * 180.0) == 1);

    CHECK(fit_distance_raw(0.0) == 0);
    CHECK(fit_distance_raw(1219.39) == 121939);  /* reference course_point */
    CHECK(fit_distance_raw(4126.89) == 412689);  /* reference lap */
    CHECK(fit_distance_raw(-5.0) == 0);
    CHECK(fit_distance_raw(1e12) == 0xFFFFFFFEu);

    CHECK(fit_altitude_raw(560.4, true) == 5302); /* reference record 0 */
    CHECK(fit_altitude_raw(221.4, true) == 3607); /* reference last record */
    CHECK(fit_altitude_raw(-500.0, true) == 0);
    CHECK(fit_altitude_raw(-600.0, true) == 0);
    CHECK(fit_altitude_raw(20000.0, true) == 0xFFFE);
    CHECK(fit_altitude_raw(123.0, false) == FIT_INVALID_UINT16);
    CHECK(fit_altitude_raw(NAN, true) == FIT_INVALID_UINT16);
}

/* ---- fixture course ----------------------------------------------------- */

#define T0 1104624000u /* 2025-01-01T00:00:00Z */
#define NTP 12

static struct trackpoint g_tp[NTP];
static struct course_pt g_cp[5];
static char g_name[128];
static char g_cpname[5][64];

/* 11 distinct points 100 m apart plus one repeated point (index 4 == 3), 72 s
 * per 100 m, elevation 500 + 3 m per step, none at distinct point 6. */
static void make_fixture(struct course *c, int with_ele)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < NTP; i++) {
        int j = i < 4 ? i : i - 1; /* index 4 repeats index 3 */
        struct trackpoint *p = &g_tp[i];

        p->lat = 38.5 + 0.001 * j;
        p->lon = -121.5 + 0.0012 * j;
        p->dist = 100.0 * j;
        p->t = T0 + 72u * (uint32_t)j;
        p->ele = 500.0 + 3.0 * j;
        p->has_ele = with_ele && j != 6;
    }
    strcpy(g_cpname[0], "Start");
    strcpy(g_cpname[1], "Water");
    strcpy(g_cpname[2], "Camp");
    strcpy(g_cpname[3], "Ranger Station");
    strcpy(g_cpname[4], "Caf\xC3\xA9 \xC3\xBC" "ber stop at the lake"); /* too long */
    {
        static const double dist[5] = { 0, 250, 600, 1000, 1000.4 };
        static const uint32_t t[5] = { T0, T0 + 180, T0 + 432, T0 + 720, T0 + 720 };
        static const uint8_t type[5] = { 0, FIT_CP_WATER, FIT_CP_CAMPSITE, 0, 5 };

        for (int k = 0; k < 5; k++) {
            g_cp[k].name = g_cpname[k];
            g_cp[k].lat = 38.5 + 0.001 * dist[k] / 100.0;
            g_cp[k].lon = -121.5 + 0.0012 * dist[k] / 100.0;
            g_cp[k].dist = dist[k];
            g_cp[k].t = t[k];
            g_cp[k].type = type[k];
        }
    }
    memset(g_name, 'N', 90);
    g_name[90] = '\0';
    c->name = g_name;
    c->tp = g_tp;
    c->n_tp = c->cap_tp = NTP;
    c->cp = g_cp;
    c->n_cp = c->cap_cp = 5;
}

static void test_fixture(const char *dir)
{
    struct course c;
    struct fit_buf b;
    struct scan s, ref;
    struct dec d;
    char sq[128], path[512];
    size_t rn;
    uint8_t *refb;

    make_fixture(&c, 1);
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_OK);
    memset(&d, 0, sizeof d);
    CHECK(scan_fit(b.data, b.len, &s, dec_cb, &d) == 0);

    /* 7.10: message order. 11 records (the repeated point is dropped), the
     * course points inserted before the first record at or past their distance
     * (the last one, past the end, after all records), events around. */
    seq_str(&s, sq, sizeof sq);
    CHECK(strcmp(sq, "ICLEprrrprrrprrrrprpE") == 0);
    if (strcmp(sq, "ICLEprrrprrrprrrrprpE") != 0)
        fprintf(stderr, "  got %s\n", sq);

    /* ... and the reference file has the same skeleton. */
    refb = slurp("tests/data/reference_course.fit", &rn);
    if (refb) {
        char rs[512];

        CHECK(scan_fit(refb, rn, &ref, NULL, NULL) == 0);
        seq_str(&ref, rs, sizeof rs);
        CHECK(strncmp(rs, "ICLEr", 5) == 0);
        CHECK(strcmp(rs + strlen(rs) - 2, "EF") == 0);
        CHECK(strstr(rs, "rpr") != NULL);
        /* 7.4/7.5/...: every definition we write is a subset of Garmin's. */
        for (int i = 0; i < s.n_defs; i++)
            CHECK(def_in_reference(&s.defs[i], &ref));
        free(refb);
    }

    /* 7.5 file_id */
    CHECK(d.n_fileid == 1 && d.ftype == FIT_FILE_TYPE_COURSE);
    CHECK(d.manu == FIT_MANUFACTURER_DEVELOPMENT && d.prod == 0);
    CHECK(d.tc == T0); /* default: time of the first trackpoint */

    /* 7.6 course: 91-byte field holding the 90-byte name */
    CHECK(d.sport == FIT_SPORT_HIKING && d.subsport == 0);
    CHECK(d.cname_size == 91);
    CHECK(memcmp(d.cname, g_name, 90) == 0 && d.cname[90] == '\0');

    /* 7.7 records */
    CHECK(d.n_rec == 11);
    for (int k = 0; k < d.n_rec && k < 11; k++) {
        const struct drec *r = &d.rec[k];
        double lat = 38.5 + 0.001 * k, lon = -121.5 + 0.0012 * k;

        CHECK(fabs(r->lat * 180.0 / 2147483648.0 - lat) < 1e-7);
        CHECK(fabs(r->lon * 180.0 / 2147483648.0 - lon) < 1e-7);
        CHECK(r->ts == T0 + 72u * (uint32_t)k);
        CHECK(r->dist == 10000u * (uint32_t)k);
        if (k == 6)
            CHECK(r->alt == FIT_INVALID_UINT16);
        else
            CHECK(fabs(r->alt / 5.0 - 500.0 - (500.0 + 3.0 * k)) < 0.11);
    }

    /* 7.8 lap */
    CHECK(d.n_lap == 1);
    CHECK(d.lap.start == T0 && d.lap.ts == T0 + 720);
    CHECK(d.lap.elapsed == 720000 && d.lap.timer == 720000);
    CHECK(d.lap.dist == 100000);
    CHECK(d.lap.avg == 1389); /* 1000 m / 720 s */
    CHECK(d.lap.asc == 30 && d.lap.desc == 0);
    CHECK(d.lap.slat == d.rec[0].lat && d.lap.slon == d.rec[0].lon);
    CHECK(d.lap.elat == d.rec[10].lat && d.lap.elon == d.rec[10].lon);
    CHECK(d.lap.swlat == d.rec[0].lat && d.lap.swlon == d.rec[0].lon);
    CHECK(d.lap.nelat == d.rec[10].lat && d.lap.nelon == d.rec[10].lon);
    CHECK(d.lap.idx == 0);

    /* events */
    CHECK(d.n_ev == 2);
    CHECK(d.ev_ts[0] == T0 && d.ev_type[0] == FIT_EVENT_TYPE_START);
    CHECK(d.ev_ts[1] == T0 + 720 &&
          d.ev_type[1] == FIT_EVENT_TYPE_STOP_DISABLE_ALL);

    /* 7.9 course points: values, index, type, name clamp */
    CHECK(d.n_cp == 5);
    for (int k = 0; k < d.n_cp && k < 5; k++) {
        CHECK(d.cp[k].idx == k);
        CHECK(d.cp[k].ts == g_cp[k].t);
        CHECK(d.cp[k].type == g_cp[k].type);
        CHECK(d.cp[k].dist == fit_distance_raw(g_cp[k].dist));
        CHECK(d.cp[k].lat == fit_semicircles(g_cp[k].lat));
        CHECK(d.cp[k].lon == fit_semicircles(g_cp[k].lon));
    }
    CHECK(strcmp(d.cp[0].name, "Start") == 0);
    CHECK(strcmp(d.cp[3].name, "Ranger Station") == 0); /* 14 bytes */
    CHECK(strlen(d.cp[4].name) <= COURSE_POINT_NAME_MAX);
    CHECK(strncmp(d.cp[4].name, "Caf\xC3\xA9 \xC3\xBC" "ber", 9) == 0);
    {   /* no cut multibyte character at the end */
        size_t n = strlen(d.cp[4].name);
        CHECK(n == 0 || ((unsigned char)d.cp[4].name[n - 1] & 0xC0) != 0xC0);
    }

    snprintf(path, sizeof path, "%s/fit_fixture_main.fit", dir);
    CHECK(fit_buf_save(&b, path) == FIT_OK);
    fit_buf_free(&b);

    /* No elevation at all: invalid altitude, ascent and descent. */
    make_fixture(&c, 0);
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_OK);
    memset(&d, 0, sizeof d);
    CHECK(scan_fit(b.data, b.len, &s, dec_cb, &d) == 0);
    CHECK(d.n_rec == 11);
    for (int k = 0; k < d.n_rec; k++)
        CHECK(d.rec[k].alt == FIT_INVALID_UINT16);
    CHECK(d.lap.asc == FIT_INVALID_UINT16 && d.lap.desc == FIT_INVALID_UINT16);
    snprintf(path, sizeof path, "%s/fit_fixture_noele.fit", dir);
    CHECK(fit_buf_save(&b, path) == FIT_OK);
    fit_buf_free(&b);
}

static void test_options_and_errors(void)
{
    struct course c;
    struct fit_buf b;
    struct fit_options o;
    struct scan s;
    struct dec d;
    char longname[200];

    /* explicit time_created and sport */
    make_fixture(&c, 1);
    fit_options_default(&o);
    o.time_created = 1234567890u;
    o.sport = 1; /* running */
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, &o, &b) == FIT_OK);
    memset(&d, 0, sizeof d);
    CHECK(scan_fit(b.data, b.len, &s, dec_cb, &d) == 0);
    CHECK(d.tc == 1234567890u && d.sport == 1);
    fit_buf_free(&b);

    /* a name over the limit is cut to 90 bytes; NULL name gets a default */
    memset(longname, 'x', sizeof longname - 1);
    longname[sizeof longname - 1] = '\0';
    c.name = longname;
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_OK);
    memset(&d, 0, sizeof d);
    CHECK(scan_fit(b.data, b.len, &s, dec_cb, &d) == 0);
    CHECK(strlen(d.cname) == 90);
    fit_buf_free(&b);

    c.name = NULL;
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_OK);
    memset(&d, 0, sizeof d);
    CHECK(scan_fit(b.data, b.len, &s, dec_cb, &d) == 0);
    CHECK(strcmp(d.cname, "Course") == 0);
    fit_buf_free(&b);

    /* no course points: no course_point definition either */
    make_fixture(&c, 1);
    c.n_cp = 0;
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_OK);
    CHECK(scan_fit(b.data, b.len, &s, NULL, NULL) == 0);
    for (int i = 0; i < s.n_defs; i++)
        CHECK(s.defs[i].global != FIT_MSG_COURSE_POINT);
    fit_buf_free(&b);

    /* too short */
    make_fixture(&c, 1);
    c.n_tp = 1;
    fit_buf_init(&b);
    CHECK(fit_encode_course(&c, NULL, &b) == FIT_TOOSHORT);
    fit_buf_free(&b);
    CHECK(strlen(fit_strerror(FIT_TOOSHORT)) > 0);
}

int main(void)
{
    const char *dir = getenv("FIT_TEST_OUT");

    if (!dir || !*dir)
        dir = "build-test";

    printf("7.1 crc\n");           test_crc(); test_crc_reference();
    printf("7.2 buffer\n");        test_buf(); test_string_field(); test_save(dir);
    printf("7.3 header/footer\n"); test_header_footer();
    printf("7.4 definition\n");    test_definition();
    printf("7.7 conversions\n");   test_conversions();
    printf("7.5-7.10 fixture\n");  test_fixture(dir);
    printf("options/errors\n");    test_options_and_errors();

    if (fails) {
        fprintf(stderr, "test_fit: %d check(s) failed\n", fails);
        return 1;
    }
    printf("test_fit: all checks passed\n");
    return 0;
}
