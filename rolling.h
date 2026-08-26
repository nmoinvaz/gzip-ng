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
   byte over the whole word, which lands boundaries far closer to their intended spacing than a
   plain shift and exclusive or does, and the top bits age out on their own so no window has to
   be tracked. A boundary is where ROLLING_HIT says so, and a mask of 2^n - 1 asks for one every
   2^n bytes on average. */
#define ROLLING_ADD(hash, byte) ((hash) = ((hash) << 1) + rolling_gear[(byte)])
#define ROLLING_HIT(hash, mask) (((hash) & (mask)) == 0)

#ifdef __cplusplus
}
#endif

#endif /* GZNG_ROLLING_H_ */
