/* scanner.h -- finding the empty stored block marker in compressed bytes
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_SCANNER_H_
#define GZNG_SCANNER_H_

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The first 00 00 FF FF at or after start, end bounding the last start position considered,
   or NULL. NEON and SSE2 filter for the zero pair the way memchr filters for a byte. */
const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end);

/* The empty stored block that continues a marker, 5 more bytes of 00 00 00 FF FF. */
static inline int32_t scan_empty_block(const uint8_t *p) {
    return memcmp(p, "\0\0\0\xff\xff", 5) == 0;
}

/* The first marker with an empty stored block right behind it, at or after start, end bounding
   the last start position considered, or NULL. end + 8 must be readable. */
const uint8_t *scan_marker_pair(const uint8_t *start, const uint8_t *end);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_SCANNER_H_ */
