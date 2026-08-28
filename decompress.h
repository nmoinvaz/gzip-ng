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
int gzng_decompress_stream(FILE *in, FILE *out, const gzng_options *opt, uint64_t *total_in, uint64_t *total_out);

/* Read the modification time and stored name from a gzip header, rewinding the stream.
   Returns 0 with the fields filled, name empty when absent, or -1 when not seekable gzip. */
int gzng_read_meta(FILE *in, uint32_t *mtime, char *name, size_t name_len);

#ifdef __cplusplus
}
#endif

#endif
