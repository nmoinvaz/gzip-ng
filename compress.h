/* compress.h -- serial gzip compression
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_COMPRESS_H_
#define GZNG_COMPRESS_H_

#include <stdint.h>
#include <stdio.h>

/* What a block holds on average when nothing says otherwise. */
#define GZNG_DEFAULT_BLOCK (128u << 10)

#ifdef __cplusplus
extern "C" {
#endif

/* Compress in to out as one gzip member, plain deflate when block_size is 0 and independent
   blocks of block_size on threads workers otherwise, the plain stream rsync friendly on request.
   Returns 0, or -1 with errno telling io errors. */
int32_t gzng_compress_stream(FILE *in, FILE *out, int32_t level, int32_t strategy, uint32_t block_size, int32_t threads,
                             int32_t rsyncable, uint32_t mtime, const char *name, uint64_t *total_in,
                             uint64_t *total_out);

#ifdef __cplusplus
}
#endif

#endif
