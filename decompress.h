/* decompress.h -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_DECOMPRESS_H_
#define GZNG_DECOMPRESS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decompress in to out, after any head bytes already taken from in, with block_size and threads
   as gzblock_reader_open() takes them. Returns 0, or -1 with the error reported to stderr. */
int32_t gzng_decompress_stream(FILE *in, FILE *out, const uint8_t *head, size_t head_len, uint32_t block_size,
                               int32_t threads, uint64_t *total_in, uint64_t *total_out);

#ifdef __cplusplus
}
#endif

#endif
