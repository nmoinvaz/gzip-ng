/* format.h -- the bytes of the gzip header and trailer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_FORMAT_H_
#define GZNG_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FORMAT_HEADER_LEN  10 /* the fixed bytes, before any optional field */
#define FORMAT_TRAILER_LEN 8  /* crc32 and size, the member ending */

/* Longest file name stored in or read from a header, including the terminator. */
#define FORMAT_NAME_MAX 256

/* Longest header format_header_build() can produce, the fixed ten bytes and the stored name. */
#define FORMAT_HEADER_MAX (FORMAT_HEADER_LEN + FORMAT_NAME_MAX)

/* The header's OS byte, RFC 1952's code for the platform the member was made on. */
#ifdef _WIN32
#  define FORMAT_OS_CODE 0
#else
#  define FORMAT_OS_CODE 3
#endif

/* Whether buf starts with the gzip magic, which two bytes decide. */
int32_t format_is_gzip(const uint8_t *buf, size_t len);

/* What a member header records. */
typedef struct {
    uint32_t mtime;          /* 0 stores no time */
    const char *name;        /* NULL or empty stores no name */
    int32_t level, strategy; /* the extra flags byte reports how hard deflate worked */
} format_header;

/* Lay a member header into buf, at most FORMAT_HEADER_MAX bytes. Returns its length. */
size_t format_header_build(uint8_t *buf, const format_header *hdr);

/* Walk a member header, over any extra field, name, comment, and header crc it carries. When
   hdr is not NULL it receives the time and the stored name, the name pointing into buf, each as
   soon as its bytes are in hand. Level and strategy are not recorded. Returns its length, 0 if
   more bytes are needed, (size_t)-1 if this is not a gzip header. */
size_t format_header_parse(const uint8_t *buf, size_t len, format_header *hdr);

/* The member trailer, crc32 of the uncompressed data and its length modulo 2^32, which is all
   gzip records however large the member was. */
void format_trailer_build(uint8_t *buf, uint32_t crc, uint64_t total);
void format_trailer_parse(const uint8_t *buf, uint32_t *crc, uint32_t *total);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_FORMAT_H_ */
