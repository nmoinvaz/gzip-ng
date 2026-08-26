/* process.h -- what gzip-ng does with each file or stream
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_PROCESS_H_
#define GZNG_PROCESS_H_

#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Filter stdin to stdout. Returns 0, or -1 with the error reported. */
int gzng_process_stdio(const gzng_options *opt);

/* Process one named file in place. Returns 0, or -1 with the error reported. */
int gzng_process_file(const char *path, const gzng_options *opt);

#ifdef __cplusplus
}
#endif

#endif
