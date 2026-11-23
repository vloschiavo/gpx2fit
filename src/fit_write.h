/*
 * fit_write.h: FIT course file encoder (phase 7).
 *
 * Output is little-endian (architecture byte 0). The Garmin reference file
 * is big-endian; both are valid FIT and every decoder must read both.
 *
 * Message order written (see NOTES.md, 0.5): file_id, course, lap, event
 * (timer start), then the records with each course_point inserted before the
 * first record whose distance is >= the point's distance, then event (timer
 * stop_disable_all). file_creator and Garmin's undocumented fields (record
 * 121/122, course 254, lap speed) are not written.
 *
 * Units on the struct course side: degrees, meters, FIT-epoch seconds.
 * Needs tp[i].dist, tp[i].t, cp[i].t (phases 3 and 6) to be filled in.
 */
#ifndef FIT_WRITE_H
#define FIT_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "course.h"

/* ---- constants ---------------------------------------------------------- */

#define FIT_PROTOCOL_VERSION 0x10 /* 1.0, as in the reference file */
#define FIT_PROFILE_VERSION  21220
#define FIT_HEADER_SIZE      14
#define FIT_CRC_SIZE         2

/* Base type codes (field definition "base type" byte). */
#define FIT_BASE_ENUM   0x00
#define FIT_BASE_UINT8  0x02
#define FIT_BASE_STRING 0x07
#define FIT_BASE_UINT16 0x84
#define FIT_BASE_SINT32 0x85
#define FIT_BASE_UINT32 0x86

/* Invalid values written when a field has no data. */
#define FIT_INVALID_UINT16 0xFFFFu
#define FIT_INVALID_UINT32 0xFFFFFFFFu
#define FIT_INVALID_SINT32 0x7FFFFFFF

/* Global message numbers. */
#define FIT_MSG_FILE_ID      0
#define FIT_MSG_LAP          19
#define FIT_MSG_RECORD       20
#define FIT_MSG_EVENT        21
#define FIT_MSG_COURSE       31
#define FIT_MSG_COURSE_POINT 32

/* Local message types, same numbering as the reference file. */
#define FIT_LOCAL_FILE_ID      0
#define FIT_LOCAL_COURSE       1
#define FIT_LOCAL_LAP          2
#define FIT_LOCAL_EVENT        3
#define FIT_LOCAL_RECORD       4
#define FIT_LOCAL_COURSE_POINT 5

/* Enum values used. */
#define FIT_FILE_TYPE_COURSE    6
#define FIT_MANUFACTURER_DEVELOPMENT 255 /* 9.1: try garmin (1) if rejected */
#define FIT_SPORT_HIKING        17
#define FIT_SUB_SPORT_GENERIC   0
#define FIT_EVENT_TIMER         0
#define FIT_EVENT_TYPE_START    0
#define FIT_EVENT_TYPE_STOP_DISABLE_ALL 9

/* Elevation noise threshold for lap ascent/descent, meters. 8.3 should use
 * the same value so the printed summary matches the file. */
#define FIT_ELE_THRESHOLD_M 1.0

/* Return codes. */
#define FIT_OK        0
#define FIT_NOMEM    (-1)
#define FIT_TOOSHORT (-2) /* fewer than 2 trackpoints */
#define FIT_TOOBIG   (-3) /* data does not fit a FIT header or message_index */
#define FIT_IOERR    (-4)

/* ---- 7.1 CRC-16 --------------------------------------------------------- */

/* FIT CRC (CRC-16/ARC, SDK nibble table). Start with crc = 0; call again with
 * the previous result to continue over more data. */
uint16_t fit_crc16(uint16_t crc, const void *data, size_t len);

/* ---- 7.2 Buffered writer ------------------------------------------------ */

/* Growable in-memory buffer, little-endian. An allocation failure sets
 * `failed`; later puts are then no-ops, so callers may check once at the end. */
struct fit_buf {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    bool     failed;
};

void fit_buf_init(struct fit_buf *b);
void fit_buf_free(struct fit_buf *b);
void fit_put_u8(struct fit_buf *b, uint8_t v);
void fit_put_u16(struct fit_buf *b, uint16_t v);
void fit_put_u32(struct fit_buf *b, uint32_t v);
void fit_put_bytes(struct fit_buf *b, const void *p, size_t n);

/* A FIT string field of exactly `size` bytes: the text, then NULs. At most
 * size - 1 bytes of text are copied and a UTF-8 sequence is never cut in the
 * middle, so the field is always NUL-terminated. NULL is an empty string. */
void fit_put_string_field(struct fit_buf *b, const char *s, size_t size);

/* Write the whole buffer to path in one go. FIT_OK or FIT_IOERR (a partly
 * written file is removed). */
int fit_buf_save(const struct fit_buf *b, const char *path);

/* ---- 7.3 Header and footer ---------------------------------------------- */

/* Append the 14-byte header with a zero data size (patched by fit_finish).
 * Call on an empty buffer. */
void fit_begin(struct fit_buf *b);

/* Patch data size and header CRC, append the file CRC. FIT_OK, FIT_NOMEM, or
 * FIT_TOOBIG. Do not put anything after this. */
int fit_finish(struct fit_buf *b);

/* ---- 7.4 Definition messages -------------------------------------------- */

struct fit_field_def {
    uint8_t num;       /* field definition number */
    uint8_t size;      /* bytes */
    uint8_t base_type; /* FIT_BASE_* */
};

/* Definition message for `local` (0..15) / `global` message number. */
void fit_put_definition(struct fit_buf *b, uint8_t local, uint16_t global,
                        const struct fit_field_def *f, size_t n);

/* ---- value conversions -------------------------------------------------- */

/* degrees -> semicircles (deg * 2^31 / 180), rounded. Clamped to
 * [INT32_MIN, 0x7FFFFFFE]; the value 0x7FFFFFFF is the "invalid" marker, so
 * +180 degrees becomes 179.9999999. Non-finite input gives the invalid value. */
int32_t fit_semicircles(double deg);

/* meters -> uint32 with scale 100, rounded; negative gives 0. */
uint32_t fit_distance_raw(double m);

/* meters -> uint16 with scale 5, offset 500, rounded and clamped to
 * 0..0xFFFE. Invalid (0xFFFF) when !has_ele or not finite. */
uint16_t fit_altitude_raw(double ele_m, bool has_ele);

/* ---- 7.5 to 7.9 Messages ------------------------------------------------ */

/* Call fit_options_default() before partially populating this struct, so
 * newly added fields (such as sub_sport) are never left uninitialized. */
struct fit_options {
    uint32_t time_created; /* file_id.time_created; 0 = first trackpoint time */
    uint8_t  sport;        /* course.sport; FIT_SPORT_HIKING by default */
    uint8_t  sub_sport;    /* course.sub_sport; FIT_SUB_SPORT_GENERIC by default */
};

void fit_options_default(struct fit_options *o);

/* Each of these writes the definition message and the data message. */
void fit_put_file_id(struct fit_buf *b, uint32_t time_created);
void fit_put_course(struct fit_buf *b, const char *name, uint8_t sport,
                    uint8_t sub_sport);
/* Needs n_tp >= 1. */
void fit_put_lap(struct fit_buf *b, const struct course *c);

/* Event data message; the definition is written first only if `define`. */
void fit_put_event(struct fit_buf *b, bool define, uint32_t ts,
                   uint8_t event_type);

void fit_put_record_def(struct fit_buf *b);
void fit_put_record(struct fit_buf *b, const struct trackpoint *tp);

void fit_put_course_point_def(struct fit_buf *b);
void fit_put_course_point(struct fit_buf *b, uint16_t index,
                          const struct course_pt *cp);

/* ---- 7.10 Whole file ---------------------------------------------------- */

/* Encode the course into *out (initialized by the caller, empty). Returns
 * FIT_OK, FIT_NOMEM, FIT_TOOSHORT or FIT_TOOBIG. opt may be NULL. */
int fit_encode_course(const struct course *c, const struct fit_options *opt,
                      struct fit_buf *out);

/* fit_encode_course + fit_buf_save. Also returns FIT_IOERR. */
int fit_write_course(const struct course *c, const struct fit_options *opt,
                     const char *path);

const char *fit_strerror(int rc);

#endif /* FIT_WRITE_H */
