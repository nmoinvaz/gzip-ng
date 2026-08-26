/* rolling.h -- content-defined boundaries for rsyncable output
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_ROLLING_H_
#define GZNG_ROLLING_H_

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

#ifdef __cplusplus
}
#endif

#endif /* GZNG_ROLLING_H_ */
