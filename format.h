/* format.h -- the bytes of the gzip header and the ZB subfield
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_FORMAT_H_
#define GZNG_FORMAT_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GZ_TRAILER 8    /* crc32 and size, the member ending */
#define ZB_PAIRED 1     /* "ZB" flags bit, block boundaries are marker pairs */

/* Returns the header length, 0 if more bytes are needed, (size_t)-1 if this is not a gzip
   header. */
size_t format_header_parse(const uint8_t *buf, size_t len, uint32_t *block_size, uint32_t *zb_flags);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_FORMAT_H_ */
