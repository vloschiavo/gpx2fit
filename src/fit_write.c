/* fit_write.c: FIT course file encoder (phase 7). See fit_write.h. */
#include "fit_write.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "geo.h"   /* course_elevation_stats */
#include "names.h" /* COURSE_NAME_MAX, COURSE_POINT_NAME_MAX (bytes, no NUL) */

/* The field size is one byte, so the string fields (limit + NUL) must fit. */
_Static_assert(COURSE_NAME_MAX + 1 <= 255, "course name field too large");
_Static_assert(COURSE_POINT_NAME_MAX + 1 <= 255,
               "course point name field too large");

#define COURSE_NAME_FIELD       (COURSE_NAME_MAX + 1)
#define COURSE_POINT_NAME_FIELD (COURSE_POINT_NAME_MAX + 1)

/* ---- 7.1 CRC-16 --------------------------------------------------------- */

uint16_t fit_crc16(uint16_t crc, const void *data, size_t len)
{
    static const uint16_t table[16] = {
        0x0000, 0xCC01, 0xD801, 0x1400, 0xF001, 0x3C00, 0x2800, 0xE401,
        0xA001, 0x6C00, 0x7800, 0xB401, 0x5000, 0x9C01, 0x8801, 0x4400
    };
    const uint8_t *p = data;

    for (size_t i = 0; i < len; i++) {
        uint16_t tmp = table[crc & 0xF];

        crc = (uint16_t)((crc >> 4) & 0x0FFF);
        crc = (uint16_t)(crc ^ tmp ^ table[p[i] & 0xF]);
        tmp = table[crc & 0xF];
        crc = (uint16_t)((crc >> 4) & 0x0FFF);
        crc = (uint16_t)(crc ^ tmp ^ table[(p[i] >> 4) & 0xF]);
    }
    return crc;
}

/* ---- 7.2 Buffered writer ------------------------------------------------ */

void fit_buf_init(struct fit_buf *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = false;
}

void fit_buf_free(struct fit_buf *b)
{
    free(b->data);
    fit_buf_init(b);
}

/* Make room for n more bytes. Returns false (and sets failed) on failure. */
static bool reserve(struct fit_buf *b, size_t n)
{
    size_t need, ncap;
    uint8_t *nd;

    if (b->failed)
        return false;
    if (n > SIZE_MAX - b->len) {
        b->failed = true;
        return false;
    }
    need = b->len + n;
    if (need <= b->cap)
        return true;
    ncap = b->cap ? b->cap : 4096;
    while (ncap < need) {
        if (ncap > SIZE_MAX / 2) {
            b->failed = true;
            return false;
        }
        ncap *= 2;
    }
    nd = realloc(b->data, ncap);
    if (!nd) {
        b->failed = true;
        return false;
    }
    b->data = nd;
    b->cap = ncap;
    return true;
}

void fit_put_bytes(struct fit_buf *b, const void *p, size_t n)
{
    if (n == 0 || !reserve(b, n))
        return;
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void fit_put_u8(struct fit_buf *b, uint8_t v)
{
    fit_put_bytes(b, &v, 1);
}

void fit_put_u16(struct fit_buf *b, uint16_t v)
{
    uint8_t t[2] = { (uint8_t)v, (uint8_t)(v >> 8) };

    fit_put_bytes(b, t, sizeof t);
}

void fit_put_u32(struct fit_buf *b, uint32_t v)
{
    uint8_t t[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
                     (uint8_t)(v >> 24) };

    fit_put_bytes(b, t, sizeof t);
}

void fit_put_string_field(struct fit_buf *b, const char *s, size_t size)
{
    size_t n = 0;

    if (size == 0)
        return;
    if (s) {
        while (n < size - 1 && s[n])
            n++;
        /* If the cut is inside a multibyte character (the next byte is a
         * continuation byte 10xxxxxx), back up to the start of that char. */
        if (s[n] && ((unsigned char)s[n] & 0xC0) == 0x80) {
            while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80)
                n--;
        }
    }
    fit_put_bytes(b, s, n);
    for (size_t i = n; i < size; i++)
        fit_put_u8(b, 0);
}

int fit_buf_save(const struct fit_buf *b, const char *path)
{
    FILE *f = fopen(path, "wb");
    int ok;

    if (!f)
        return FIT_IOERR;
    ok = fwrite(b->data, 1, b->len, f) == b->len;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(path);
        return FIT_IOERR;
    }
    return FIT_OK;
}

/* ---- 7.3 Header and footer ---------------------------------------------- */

void fit_begin(struct fit_buf *b)
{
    fit_put_u8(b, FIT_HEADER_SIZE);
    fit_put_u8(b, FIT_PROTOCOL_VERSION);
    fit_put_u16(b, FIT_PROFILE_VERSION);
    fit_put_u32(b, 0); /* data size, patched in fit_finish */
    fit_put_bytes(b, ".FIT", 4);
    fit_put_u16(b, 0); /* header CRC, patched in fit_finish */
}

static void patch_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

int fit_finish(struct fit_buf *b)
{
    size_t data_size;
    uint16_t crc;

    if (b->failed)
        return FIT_NOMEM;
    if (b->len < FIT_HEADER_SIZE)
        return FIT_TOOBIG; /* fit_begin was not called */
    data_size = b->len - FIT_HEADER_SIZE;
    if (data_size > UINT32_MAX)
        return FIT_TOOBIG;
    patch_u32(b->data + 4, (uint32_t)data_size);
    crc = fit_crc16(0, b->data, 12);
    b->data[12] = (uint8_t)crc;
    b->data[13] = (uint8_t)(crc >> 8);
    /* The file CRC covers the whole header (with its CRC) and the data. */
    crc = fit_crc16(0, b->data, b->len);
    fit_put_u16(b, crc);
    return b->failed ? FIT_NOMEM : FIT_OK;
}

/* ---- 7.4 Definition messages -------------------------------------------- */

void fit_put_definition(struct fit_buf *b, uint8_t local, uint16_t global,
                        const struct fit_field_def *f, size_t n)
{
    fit_put_u8(b, (uint8_t)(0x40 | (local & 0x0F)));
    fit_put_u8(b, 0); /* reserved */
    fit_put_u8(b, 0); /* architecture: 0 = little-endian */
    fit_put_u16(b, global);
    fit_put_u8(b, (uint8_t)n);
    for (size_t i = 0; i < n; i++) {
        fit_put_u8(b, f[i].num);
        fit_put_u8(b, f[i].size);
        fit_put_u8(b, f[i].base_type);
    }
}

/* Header byte of a data message. */
static void put_data_header(struct fit_buf *b, uint8_t local)
{
    fit_put_u8(b, (uint8_t)(local & 0x0F));
}

/* ---- value conversions -------------------------------------------------- */

int32_t fit_semicircles(double deg)
{
    double v;
    long long r;

    if (!isfinite(deg))
        return FIT_INVALID_SINT32;
    v = deg * (2147483648.0 / 180.0);
    if (v <= -2147483648.0)
        return INT32_MIN;
    if (v >= 2147483646.0)
        return 0x7FFFFFFE;
    r = llround(v);
    if (r > 0x7FFFFFFE)
        r = 0x7FFFFFFE;
    return (int32_t)r;
}

uint32_t fit_distance_raw(double m)
{
    double v;

    if (!(m > 0.0)) /* negative, zero, or NaN */
        return 0;
    v = m * 100.0;
    if (v >= 4294967294.0)
        return 0xFFFFFFFEu;
    return (uint32_t)llround(v);
}

uint16_t fit_altitude_raw(double ele_m, bool has_ele)
{
    double v;

    if (!has_ele || !isfinite(ele_m))
        return FIT_INVALID_UINT16;
    v = (ele_m + 500.0) * 5.0;
    if (v <= 0.0)
        return 0;
    if (v >= 65534.0)
        return 0xFFFE;
    return (uint16_t)llround(v);
}

static uint16_t clamp_u16(double v)
{
    if (!(v > 0.0))
        return 0;
    if (v >= 65534.0)
        return 0xFFFE;
    return (uint16_t)llround(v);
}

static uint32_t clamp_u32(double v)
{
    if (!(v > 0.0))
        return 0;
    if (v >= 4294967294.0)
        return 0xFFFFFFFEu;
    return (uint32_t)llround(v);
}

/* ---- 7.5 file_id -------------------------------------------------------- */

void fit_options_default(struct fit_options *o)
{
    o->time_created = 0;
    o->sport = FIT_SPORT_HIKING;
    o->sub_sport = FIT_SUB_SPORT_GENERIC;
}

void fit_put_file_id(struct fit_buf *b, uint32_t time_created)
{
    /* Same field order as the reference, minus serial_number and number. */
    static const struct fit_field_def def[] = {
        { 0, 1, FIT_BASE_ENUM },   /* type */
        { 1, 2, FIT_BASE_UINT16 }, /* manufacturer */
        { 2, 2, FIT_BASE_UINT16 }, /* product */
        { 4, 4, FIT_BASE_UINT32 }, /* time_created */
    };

    fit_put_definition(b, FIT_LOCAL_FILE_ID, FIT_MSG_FILE_ID, def,
                       sizeof def / sizeof *def);
    put_data_header(b, FIT_LOCAL_FILE_ID);
    fit_put_u8(b, FIT_FILE_TYPE_COURSE);
    fit_put_u16(b, FIT_MANUFACTURER_DEVELOPMENT);
    fit_put_u16(b, 0);
    fit_put_u32(b, time_created);
}

/* ---- 7.6 course --------------------------------------------------------- */

void fit_put_course(struct fit_buf *b, const char *name, uint8_t sport,
                    uint8_t sub_sport)
{
    static const struct fit_field_def def[] = {
        { 4, 1, FIT_BASE_ENUM },                              /* sport */
        { 5, COURSE_NAME_FIELD, FIT_BASE_STRING },            /* name */
        { 7, 1, FIT_BASE_ENUM },                              /* sub_sport */
    };

    fit_put_definition(b, FIT_LOCAL_COURSE, FIT_MSG_COURSE, def,
                       sizeof def / sizeof *def);
    put_data_header(b, FIT_LOCAL_COURSE);
    fit_put_u8(b, sport);
    fit_put_string_field(b, (name && name[0]) ? name : "Course",
                         COURSE_NAME_FIELD);
    fit_put_u8(b, sub_sport);
}

/* ---- 7.8 lap ------------------------------------------------------------ */

void fit_put_lap(struct fit_buf *b, const struct course *c)
{
    /* Field order of the reference lap (NOTES.md, 0.4), without the
     * undocumented/derived ones and without speed fields except avg_speed. */
    static const struct fit_field_def def[] = {
        { 2,   4, FIT_BASE_UINT32 }, /* start_time */
        { 253, 4, FIT_BASE_UINT32 }, /* timestamp */
        { 3,   4, FIT_BASE_SINT32 }, /* start_position_lat */
        { 4,   4, FIT_BASE_SINT32 }, /* start_position_long */
        { 5,   4, FIT_BASE_SINT32 }, /* end_position_lat */
        { 6,   4, FIT_BASE_SINT32 }, /* end_position_long */
        { 21,  2, FIT_BASE_UINT16 }, /* total_ascent */
        { 22,  2, FIT_BASE_UINT16 }, /* total_descent */
        { 29,  4, FIT_BASE_SINT32 }, /* swc_lat */
        { 30,  4, FIT_BASE_SINT32 }, /* swc_long */
        { 27,  4, FIT_BASE_SINT32 }, /* nec_lat */
        { 28,  4, FIT_BASE_SINT32 }, /* nec_long */
        { 13,  2, FIT_BASE_UINT16 }, /* avg_speed, scale 1000 */
        { 7,   4, FIT_BASE_UINT32 }, /* total_elapsed_time, scale 1000 */
        { 8,   4, FIT_BASE_UINT32 }, /* total_timer_time, scale 1000 */
        { 9,   4, FIT_BASE_UINT32 }, /* total_distance, scale 100 */
        { 254, 2, FIT_BASE_UINT16 }, /* message_index */
    };
    const struct trackpoint *first = &c->tp[0];
    const struct trackpoint *last = &c->tp[c->n_tp - 1];
    double lat_min = first->lat, lat_max = first->lat;
    double lon_min = first->lon, lon_max = first->lon;
    struct ele_stats es;
    uint32_t elapsed_s, ascent, descent;
    double avg_speed = 0.0;

    /* Bounding box. A track crossing the antimeridian gets a box that spans
     * the whole globe; harmless for a course file. */
    for (size_t i = 1; i < c->n_tp; i++) {
        const struct trackpoint *p = &c->tp[i];

        if (p->lat < lat_min) lat_min = p->lat;
        if (p->lat > lat_max) lat_max = p->lat;
        if (p->lon < lon_min) lon_min = p->lon;
        if (p->lon > lon_max) lon_max = p->lon;
    }

    course_elevation_stats(c, FIT_ELE_THRESHOLD_M, &es);
    ascent = es.valid ? clamp_u16(es.ascent) : FIT_INVALID_UINT16;
    descent = es.valid ? clamp_u16(es.descent) : FIT_INVALID_UINT16;

    elapsed_s = last->t >= first->t ? last->t - first->t : 0;
    if (elapsed_s > 0)
        avg_speed = last->dist / (double)elapsed_s;

    fit_put_definition(b, FIT_LOCAL_LAP, FIT_MSG_LAP, def,
                       sizeof def / sizeof *def);
    put_data_header(b, FIT_LOCAL_LAP);
    fit_put_u32(b, first->t);                          /* start_time */
    fit_put_u32(b, last->t);                           /* timestamp: lap end */
    fit_put_u32(b, (uint32_t)fit_semicircles(first->lat));
    fit_put_u32(b, (uint32_t)fit_semicircles(first->lon));
    fit_put_u32(b, (uint32_t)fit_semicircles(last->lat));
    fit_put_u32(b, (uint32_t)fit_semicircles(last->lon));
    fit_put_u16(b, (uint16_t)ascent);
    fit_put_u16(b, (uint16_t)descent);
    fit_put_u32(b, (uint32_t)fit_semicircles(lat_min)); /* swc */
    fit_put_u32(b, (uint32_t)fit_semicircles(lon_min));
    fit_put_u32(b, (uint32_t)fit_semicircles(lat_max)); /* nec */
    fit_put_u32(b, (uint32_t)fit_semicircles(lon_max));
    fit_put_u16(b, clamp_u16(avg_speed * 1000.0));
    fit_put_u32(b, clamp_u32((double)elapsed_s * 1000.0));
    fit_put_u32(b, clamp_u32((double)elapsed_s * 1000.0));
    fit_put_u32(b, fit_distance_raw(last->dist));
    fit_put_u16(b, 0);                                 /* message_index */
}

/* ---- event -------------------------------------------------------------- */

void fit_put_event(struct fit_buf *b, bool define, uint32_t ts,
                   uint8_t event_type)
{
    static const struct fit_field_def def[] = {
        { 253, 4, FIT_BASE_UINT32 }, /* timestamp */
        { 0,   1, FIT_BASE_ENUM },   /* event */
        { 4,   1, FIT_BASE_UINT8 },  /* event_group */
        { 1,   1, FIT_BASE_ENUM },   /* event_type */
    };

    if (define)
        fit_put_definition(b, FIT_LOCAL_EVENT, FIT_MSG_EVENT, def,
                           sizeof def / sizeof *def);
    put_data_header(b, FIT_LOCAL_EVENT);
    fit_put_u32(b, ts);
    fit_put_u8(b, FIT_EVENT_TIMER);
    fit_put_u8(b, 0);
    fit_put_u8(b, event_type);
}

/* ---- 7.7 record --------------------------------------------------------- */

void fit_put_record_def(struct fit_buf *b)
{
    static const struct fit_field_def def[] = {
        { 0,   4, FIT_BASE_SINT32 }, /* position_lat */
        { 1,   4, FIT_BASE_SINT32 }, /* position_long */
        { 253, 4, FIT_BASE_UINT32 }, /* timestamp */
        { 5,   4, FIT_BASE_UINT32 }, /* distance, scale 100 */
        { 2,   2, FIT_BASE_UINT16 }, /* altitude, scale 5, offset 500 */
    };

    fit_put_definition(b, FIT_LOCAL_RECORD, FIT_MSG_RECORD, def,
                       sizeof def / sizeof *def);
}

void fit_put_record(struct fit_buf *b, const struct trackpoint *tp)
{
    put_data_header(b, FIT_LOCAL_RECORD);
    fit_put_u32(b, (uint32_t)fit_semicircles(tp->lat));
    fit_put_u32(b, (uint32_t)fit_semicircles(tp->lon));
    fit_put_u32(b, tp->t);
    fit_put_u32(b, fit_distance_raw(tp->dist));
    fit_put_u16(b, fit_altitude_raw(tp->ele, tp->has_ele));
}

/* ---- 7.9 course_point --------------------------------------------------- */

void fit_put_course_point_def(struct fit_buf *b)
{
    /* Reference order: 254, 1, 5, 2, 3, 4, 6. */
    static const struct fit_field_def def[] = {
        { 254, 2, FIT_BASE_UINT16 },                           /* message_index */
        { 1,   4, FIT_BASE_UINT32 },                           /* timestamp */
        { 5,   1, FIT_BASE_ENUM },                             /* type */
        { 2,   4, FIT_BASE_SINT32 },                           /* position_lat */
        { 3,   4, FIT_BASE_SINT32 },                           /* position_long */
        { 4,   4, FIT_BASE_UINT32 },                           /* distance */
        { 6, COURSE_POINT_NAME_FIELD, FIT_BASE_STRING },       /* name */
    };

    fit_put_definition(b, FIT_LOCAL_COURSE_POINT, FIT_MSG_COURSE_POINT, def,
                       sizeof def / sizeof *def);
}

void fit_put_course_point(struct fit_buf *b, uint16_t index,
                          const struct course_pt *cp)
{
    put_data_header(b, FIT_LOCAL_COURSE_POINT);
    fit_put_u16(b, index);
    fit_put_u32(b, cp->t);
    fit_put_u8(b, cp->type);
    fit_put_u32(b, (uint32_t)fit_semicircles(cp->lat));
    fit_put_u32(b, (uint32_t)fit_semicircles(cp->lon));
    fit_put_u32(b, fit_distance_raw(cp->dist));
    fit_put_string_field(b, cp->name, COURSE_POINT_NAME_FIELD);
}

/* ---- 7.10 Whole file ---------------------------------------------------- */

int fit_encode_course(const struct course *c, const struct fit_options *opt,
                      struct fit_buf *out)
{
    struct fit_options o;
    const struct trackpoint *prev = NULL;
    size_t next_cp = 0;
    uint32_t first_t, last_t;

    if (opt)
        o = *opt;
    else
        fit_options_default(&o);
    if (c->n_tp < 2)
        return FIT_TOOSHORT;
    if (c->n_cp > 0xFFFF)
        return FIT_TOOBIG; /* message_index is a uint16 */

    first_t = c->tp[0].t;
    last_t = c->tp[c->n_tp - 1].t;
    if (o.time_created == 0)
        o.time_created = first_t;

    fit_begin(out);
    fit_put_file_id(out, o.time_created);
    fit_put_course(out, c->name, o.sport, o.sub_sport);
    fit_put_lap(out, c);
    fit_put_event(out, true, first_t, FIT_EVENT_TYPE_START);

    fit_put_record_def(out);
    if (c->n_cp > 0)
        fit_put_course_point_def(out);

    for (size_t i = 0; i < c->n_tp; i++) {
        const struct trackpoint *p = &c->tp[i];

        /* Repeated points (zero-length segments) would give records with
         * identical position, distance and time; the reference has none. */
        if (prev && p->dist == prev->dist && p->lat == prev->lat &&
            p->lon == prev->lon)
            continue;
        /* Course points go before the first record at or past their distance;
         * c->cp is sorted by dist. */
        while (next_cp < c->n_cp && c->cp[next_cp].dist <= p->dist) {
            fit_put_course_point(out, (uint16_t)next_cp, &c->cp[next_cp]);
            next_cp++;
        }
        fit_put_record(out, p);
        prev = p;
    }
    /* Course points beyond the last record's distance (rounding): keep them. */
    while (next_cp < c->n_cp) {
        fit_put_course_point(out, (uint16_t)next_cp, &c->cp[next_cp]);
        next_cp++;
    }

    fit_put_event(out, false, last_t, FIT_EVENT_TYPE_STOP_DISABLE_ALL);
    return fit_finish(out);
}

int fit_write_course(const struct course *c, const struct fit_options *opt,
                     const char *path)
{
    struct fit_buf b;
    int rc;

    fit_buf_init(&b);
    rc = fit_encode_course(c, opt, &b);
    if (rc == FIT_OK)
        rc = fit_buf_save(&b, path);
    fit_buf_free(&b);
    return rc;
}

const char *fit_strerror(int rc)
{
    switch (rc) {
    case FIT_OK:        return "ok";
    case FIT_NOMEM:     return "out of memory";
    case FIT_TOOSHORT:  return "course has fewer than 2 trackpoints";
    case FIT_TOOBIG:    return "course is too large for a FIT file";
    case FIT_IOERR:     return "cannot write the FIT file";
    default:            return "unknown FIT encoder error";
    }
}
