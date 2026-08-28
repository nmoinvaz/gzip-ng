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

/* The scalar scanner, also the tail behind the vector ones. Pointer to the first 00 00 FF FF
   starting in [start, end), or NULL. end + 3 must be readable. */
static const uint8_t *scan_marker_scalar(const uint8_t *start, const uint8_t *end) {
    while (start < end && (start = (const uint8_t *)memchr(start, 0, (size_t)(end - start)))) {
        if (start[1] == 0 && start[2] == 0xff && start[3] == 0xff)
            return start;
        start++;
    }
    return NULL;
}

/* Pointer to the first 00 00 FF FF starting in [start, end), or NULL. end + 3 must be readable.
   The vector versions filter for the zero byte the way libc memchr does, one load and one
   reduction per 16 bytes, but stay inline so the roughly one hit per 256 bytes that compressed
   data produces costs a few cycles instead of a call boundary. Candidates get the full four-byte
   check, real markers are kilobytes apart. */
#if !defined(GZBLOCK_NO_SIMD) && (defined(__aarch64__) || defined(_M_ARM64))

#  include <arm_neon.h>

const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end) {
    while (end - start >= 16) {
        uint8x16_t v = vld1q_u8(start);
        if (vminvq_u8(v) == 0) {
            /* four mask bits per lane, the usual movemask substitute */
            uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(vceqzq_u8(v)), 4)), 0);
            do {
                unsigned i = gzng_ctz64(mask) >> 2;
                const uint8_t *q = start + i;
                if (q[1] == 0 && q[2] == 0xff && q[3] == 0xff)
                    return q;
                mask &= ~(0xfull << (i * 4));
            } while (mask != 0);
        }
        start += 16;
    }
    return scan_marker_scalar(start, end);
}

#elif !defined(GZBLOCK_NO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))

#  include <emmintrin.h>

const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end) {
    const __m128i zero = _mm_setzero_si128();
    while (end - start >= 16) {
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)start), zero));
        while (mask != 0) {
            unsigned i = gzng_ctz32(mask);
            const uint8_t *q = start + i;
            if (q[1] == 0 && q[2] == 0xff && q[3] == 0xff)
                return q;
            mask &= mask - 1;
        }
        start += 16;
    }
    return scan_marker_scalar(start, end);
}

#else

const uint8_t *scan_marker(const uint8_t *start, const uint8_t *end) {
    return scan_marker_scalar(start, end);
}

#endif
