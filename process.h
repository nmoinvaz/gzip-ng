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

/* Filter stdin to stdout. */
int gzng_process_stdio(const gzng_options *opt);

/* Process one file, never a directory. */
int gzng_process_file(const char *path, const gzng_options *opt);

/* Check one file's integrity without writing anything, the --test verdict. */
int gzng_test_file(const char *path, const gzng_options *opt);

#ifdef __cplusplus
}
#endif

#endif
