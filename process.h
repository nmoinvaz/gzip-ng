/* process.h -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_PROCESS_H_
#define GZNG_PROCESS_H_

#include <stdint.h>

#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Filter stdin to stdout. Returns 0, 1 on error, with the error reported. */
int gzng_process_stdio(const gzng_options *opt);

/* Process one file, never a directory. Returns 0, 1 on error, 2 on a warning, reported either
   way. */
int gzng_process_file(const char *path, const gzng_options *opt);

/* Check one file's integrity without writing anything, the --test verdict. Returns 0, or 1 with
   the error reported. */
int gzng_test_file(const char *path, const gzng_options *opt);

/* Whether path ends in the .gz suffix. */
int gzng_path_has_suffix(const char *path);

#ifdef __cplusplus
}
#endif

#endif
