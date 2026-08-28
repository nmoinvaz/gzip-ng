/* list.h -- the gzip listing
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_LIST_H_
#define GZNG_LIST_H_

#include <stdint.h>

#include "options.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t compressed;
    uint64_t uncompressed;
    int files;
} gzng_totals;

/* List one compressed file the way gzip --list does, accumulating totals. */
int gzng_list_file(const char *path, const gzng_options *opt, gzng_totals *totals);

/* The header row, and the totals row once more than one file was listed. */
void gzng_list_begin(const gzng_options *opt);
void gzng_list_end(const gzng_options *opt, const gzng_totals *totals);

#ifdef __cplusplus
}
#endif

#endif
