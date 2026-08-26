/* util.h -- small things more than one module wants
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_UTIL_H_
#define GZNG_UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Both arguments are evaluated twice, so keep side effects out of them. */
#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) < (b) ? (b) : (a))

/* Reverse the bytes in a value, the compiler's own where it has one. */
#if defined(_MSC_VER)
#  include <stdlib.h>
#  define BSWAP16(v) _byteswap_ushort(v)
#  define BSWAP32(v) _byteswap_ulong(v)
#elif defined(__clang__) || defined(__GNUC__)
#  define BSWAP16(v) __builtin_bswap16(v)
#  define BSWAP32(v) __builtin_bswap32(v)
#else
#  define BSWAP16(v) (uint16_t)(((v) >> 8) | ((v) << 8))
#  define BSWAP32(v)                                                                                                   \
      (uint32_t)((((v) >> 24) & 0xff) | (((v) >> 8) & 0xff00) | (((v) & 0xff00) << 8) | (((v) & 0xff) << 24))
#endif

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#  define GZNG_BIG_ENDIAN 1
#endif

/* gzip counts in little endian, so a value is reversed on a machine that does not, and the bytes
   themselves are moved with memcpy, which every compiler turns into the load or store it is. */
static inline void store_le16(uint8_t *buf, uint16_t v) {
#ifdef GZNG_BIG_ENDIAN
    v = BSWAP16(v);
#endif
    memcpy(buf, &v, sizeof(v));
}

static inline void store_le32(uint8_t *buf, uint32_t v) {
#ifdef GZNG_BIG_ENDIAN
    v = BSWAP32(v);
#endif
    memcpy(buf, &v, sizeof(v));
}

static inline uint16_t load_le16(const uint8_t *buf) {
    uint16_t v;
    memcpy(&v, buf, sizeof(v));
#ifdef GZNG_BIG_ENDIAN
    v = BSWAP16(v);
#endif
    return v;
}

static inline uint32_t load_le32(const uint8_t *buf) {
    uint32_t v;
    memcpy(&v, buf, sizeof(v));
#ifdef GZNG_BIG_ENDIAN
    v = BSWAP32(v);
#endif
    return v;
}

/* zlib measures its buffers in 32 bits, so a size_t goes over a chunk at a time. */
static inline uint32_t clamp_u32(size_t n) {
    return n > UINT32_MAX ? UINT32_MAX : (uint32_t)n;
}

#endif /* GZNG_UTIL_H_ */
