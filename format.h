/* format.h -- the bytes of the gzip header and trailer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_FORMAT_H_
#define GZNG_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

#include "gzblock.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GZ_TRAILER 8 /* crc32 and size, the member ending */

/* Longest header format_header_build() can produce, the fixed ten bytes and the stored name. */
#define FORMAT_HEADER_MAX (10 + GZBLOCK_NAME_MAX)

/* What a member header records. */
typedef struct {
    uint32_t mtime;      /* 0 stores no time */
    const char *name;    /* NULL or empty stores no name */
    int level, strategy; /* the extra flags byte reports how hard deflate worked */
} format_header;

/* Lay a member header into buf, at most FORMAT_HEADER_MAX bytes. Returns its length. */
size_t format_header_build(uint8_t *buf, const format_header *hdr);

/* Walk a member header, over any extra field, name, comment, and header crc it carries.
   Returns its length, 0 if more bytes are needed, (size_t)-1 if this is not a gzip header. */
size_t format_header_parse(const uint8_t *buf, size_t len);

/* The member trailer, crc32 of the uncompressed data and its length modulo 2^32, which is all
   gzip records however large the member was. */
void format_trailer_build(uint8_t *buf, uint32_t crc, uint64_t total);
void format_trailer_parse(const uint8_t *buf, uint32_t *crc, uint32_t *total);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_FORMAT_H_ */
