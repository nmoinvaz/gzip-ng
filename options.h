/* options.h -- gzip-ng command line options
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_OPTIONS_H_
#define GZNG_OPTIONS_H_

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int decompress;      /* -d */
    int stdout_mode;     /* -c */
    int keep;            /* -k */
    int force;           /* -f, overwrite outputs, write compressed data to a terminal */
    int recursive;       /* -r, descend into directories */
    int verbose;         /* -v, report each file processed */
    int quiet;           /* -q, suppress warnings */
    int name_mode;       /* -1 default, 0 with -n, 1 with -N */
    int level;           /* -0 to -9 */
    int strategy;        /* deflate strategy options */
    int transparent;     /* -T, copy without compressing */
    int text_mode;       /* -A, no effect where text and binary io agree */
    uint32_t block_size; /* -b, 0 writes one plain deflate stream */
    int threads;         /* -p, 0 picks the number of CPUs */
} gzng_options;

void gzng_options_init(gzng_options *opt);

/* Preset options from the program name, gunzip, zcat, and gzcat act as gzip's aliases do. */
void gzng_options_personas(gzng_options *opt, const char *argv0);

/* Parse the command line the way gzip does, options and files in any order, short options
   clustered, values attached or separate, -- ending the options. File arguments are compacted
   into argv[1..*nfiles]. Returns 0 to process files, 1 when the run is already complete
   (--help, --version), or -1 on a bad command line with a message printed. */
int gzng_options_parse(gzng_options *opt, int argc, char **argv, int *nfiles);

void gzng_usage(FILE *out);

/* Parse a size with an optional K, M, or G suffix. Returns 0 when it is not usable. */
uint32_t gzng_parse_size(const char *arg);

#ifdef __cplusplus
}
#endif

#endif
