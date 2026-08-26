/* scanner.h -- finding the empty stored block marker in compressed bytes
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_SCANNER_H_
#define GZNG_SCANNER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The first 00 00 FF FF at or after start, end bounding the last start position considered,
   or NULL. NEON and SSE2 filter for the zero pair the way memchr filters for a byte. */
const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_SCANNER_H_ */
