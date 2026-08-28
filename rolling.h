/* rolling.h -- content-defined boundaries for rsyncable output
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_ROLLING_H_
#define GZNG_ROLLING_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint32_t rolling_gear[256];

/* Gear, the hash FastCDC uses, one shift and one table lookup per byte. The table spreads each
   byte across the word, landing boundaries far closer to their intended spacing than a plain
   shift and exclusive or, and old bytes age out of the top so no window is tracked. A mask of
   2^n - 1 asks for a boundary every 2^n bytes on average. */
#define ROLLING_ADD(hash, byte) ((hash) = ((hash) << 1) + rolling_gear[(byte)])
#define ROLLING_HIT(hash, mask) (((hash) & (mask)) == 0)

/* Closest and furthest apart boundaries are ever asked to fall. Below the minimum a sync point
   costs more than the sharing it buys, which is the spacing gzip and pigz settled on for
   rsyncable output, and the maximum is where a 32 bit mask runs out of usable bits. */
#define ROLLING_MIN_SPAN 4096u
#define ROLLING_MAX_SPAN (16u << 20)

/* The mask that asks for a boundary about every span bytes, rounded up to a power of two and
   held between the two. */
static inline uint32_t rolling_mask(size_t span) {
    uint32_t bits = 0;

    if (span < ROLLING_MIN_SPAN)
        span = ROLLING_MIN_SPAN;
    if (span > ROLLING_MAX_SPAN)
        span = ROLLING_MAX_SPAN;
    while (((size_t)1 << bits) < span)
        bits++;
    return ((uint32_t)1 << bits) - 1;
}

/* The first position in [first, len) whose hash hits mask, or len when none does. hash carries
   the chain in and comes out as the hash at the position returned, or at the last byte hashed.
   Since the hash forgets a byte after 32 shifts, hashing starts 31 bytes before first, and the
   chain carried in matters only when first is closer to the start than that. */
size_t rolling_find(uint32_t *hash, uint32_t mask, const uint8_t *buf, size_t len, size_t first);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_ROLLING_H_ */
