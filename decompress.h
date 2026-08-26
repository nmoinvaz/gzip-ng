/* decompress.h -- decompression through the block engine reader
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_DECOMPRESS_H_
#define GZNG_DECOMPRESS_H_

#include <stdint.h>
#include <stdio.h>

#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decompress in to out. Returns 0, or -1 with the error reported to stderr. */
int gzng_decompress_stream(FILE *in, FILE *out, const gzng_options *opt,
                           uint64_t *in_len, uint64_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
