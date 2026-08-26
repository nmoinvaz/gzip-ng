/* util.h -- small things more than one module wants
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_UTIL_H_
#define GZNG_UTIL_H_

#include <stddef.h>
#include <stdint.h>

/* Both arguments are evaluated twice, so keep side effects out of them. */
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

/* zlib measures its buffers in 32 bits, so a size_t goes over a chunk at a time. */
static inline uint32_t clamp_u32(size_t n) {
    return n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
}

#endif /* GZNG_UTIL_H_ */
