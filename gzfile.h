/* gzfile.h -- gzip files by name and by header
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_GZFILE_H_
#define GZNG_GZFILE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The suffix that names a gzip file. */
#define GZ_SUFFIX     ".gz"
#define GZ_SUFFIX_LEN 3

/* Whether path ends in the suffix. */
int gzng_path_has_suffix(const char *path);

/* Read the modification time and stored name from a gzip header, rewinding the stream.
   Returns 0 with the fields filled, name empty when absent, or -1 when not seekable gzip. */
int gzng_read_meta(FILE *in, uint32_t *mtime, char *name, size_t name_len);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_GZFILE_H_ */
