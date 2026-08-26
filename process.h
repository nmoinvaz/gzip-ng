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

/* Process one path, a file or, with -r, a directory. Returns 0, 1 on error, 2 on a warning,
   reported either way. */
int gzng_process_path(const char *path, const gzng_options *opt);

#ifdef __cplusplus
}
#endif

#endif
