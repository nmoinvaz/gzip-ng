/* gzfile.h -- the names of gzip files
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_GZFILE_H_
#define GZNG_GZFILE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GZ_SUFFIX     ".gz"
#define GZ_SUFFIX_LEN 3
#define GZ_PATH_MAX   4096

/* Whether path ends in the suffix. */
int gzng_path_has_suffix(const char *path);

/* Compression turns file into file.gz, decompression file.gz into file, or file into file by
   reading file.gz. Returns -1 with errno set when the name will not fit. */
int gzng_path_derive(const char *path, int decompress, char *in_path, char *out_path, size_t cap);

/* The output name for --name, the stored name placed in the input's directory. */
void gzng_path_from_stored(char *out_path, size_t cap, const char *in_path, const char *stored);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_GZFILE_H_ */
