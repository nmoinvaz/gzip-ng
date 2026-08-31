/* scanner.c -- finding the empty stored block marker in compressed bytes
 * For conditions of distribution and use, see LICENSE.md
 */

#include "scanner.h"

#include <string.h>

/* Trailing zero counts for the marker scanners. */
static inline uint32_t gzng_ctz32(uint32_t v) {
    return (uint32_t)__builtin_ctz(v);
}
static inline uint32_t gzng_ctz64(uint64_t v) {
    return (uint32_t)__builtin_ctzll(v);
}

/* The check behind the zero filter, the marker's remaining bytes, and for the pair scan the
   empty stored block behind them. pair is a constant at every call, so each caller gets its own
   specialized scan. */
static inline int32_t scan_hit(const uint8_t *q, int32_t pair) {
    if (q[1] != 0 || q[2] != 0xff || q[3] != 0xff)
        return 0;
    return !pair || scan_empty_block(q + 4);
}

/* The scalar scan, also the tail behind the vector ones. Pointer to the first hit starting in
   [start, end), or NULL. end + 3 must be readable, end + 8 for the pair scan. */
static inline const uint8_t *scan_scalar(const uint8_t *start, const uint8_t *end, int32_t pair) {
    while (start < end && (start = (const uint8_t *)memchr(start, 0, (size_t)(end - start)))) {
        if (scan_hit(start, pair))
            return start;
        start++;
    }
    return NULL;
}

/* Pointer to the first hit starting in [start, end), or NULL, readable bounds as in scan_scalar.
   The vector versions filter for the zero byte the way libc memchr does, one load and one
   reduction per 16 bytes, but stay inline so the roughly one hit per 256 bytes that compressed
   data produces costs a few cycles instead of a call boundary. Candidates get the full byte
   check, real markers are kilobytes apart. */
#if !defined(GZBLOCK_NO_SIMD) && (defined(__aarch64__) || defined(_M_ARM64))

#  include <arm_neon.h>

static inline const uint8_t *scan(const uint8_t *start, const uint8_t *end, int32_t pair) {
    while (end - start >= 16) {
        uint8x16_t v = vld1q_u8(start);
        if (vminvq_u8(v) == 0) {
            /* four mask bits per lane, the usual movemask substitute */
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(vceqzq_u8(v)), 4)), 0);
            do {
                unsigned i = gzng_ctz64(mask) >> 2;
                const uint8_t *q = start + i;
                if (scan_hit(q, pair))
                    return q;
                mask &= ~(0xfull << (i * 4));
            } while (mask != 0);
        }
        start += 16;
    }
    return scan_scalar(start, end, pair);
}

#elif !defined(GZBLOCK_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))

#  include <emmintrin.h>

static inline const uint8_t *scan(const uint8_t *start, const uint8_t *end, int32_t pair) {
    const __m128i zero = _mm_setzero_si128();
    while (end - start >= 16) {
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)start), zero));
        while (mask != 0) {
            unsigned i = gzng_ctz32(mask);
            const uint8_t *q = start + i;
            if (scan_hit(q, pair))
                return q;
            mask &= mask - 1;
        }
        start += 16;
    }
    return scan_scalar(start, end, pair);
}

#else

static inline const uint8_t *scan(const uint8_t *start, const uint8_t *end, int32_t pair) {
    return scan_scalar(start, end, pair);
}

#endif

const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end) {
    return scan(start, end, 0);
}

const uint8_t *scan_marker_pair(const uint8_t *start, const uint8_t *end) {
    return scan(start, end, 1);
}
