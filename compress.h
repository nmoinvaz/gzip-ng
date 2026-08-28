/* compress.h -- serial gzip compression
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_COMPRESS_H_
#define GZNG_COMPRESS_H_

#include <stdint.h>
#include <stdio.h>

#include "options.h"

/* What a block holds on average when nothing says otherwise. */
#define GZNG_DEFAULT_BLOCK (128u << 10)

#ifdef __cplusplus
extern "C" {
#endif

/* Compress in to out as one plain gzip stream. Returns 0, or -1 with errno telling io errors. */
int gzng_compress_stream(FILE *in, FILE *out, const gzng_options *opt, uint32_t mtime, const char *name,
                         uint64_t *total_in, uint64_t *total_out);

#ifdef __cplusplus
}
#endif

#endif
