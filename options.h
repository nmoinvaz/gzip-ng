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
    int32_t decompress;    /* --decompress */
    int32_t stdout_mode;   /* --stdout */
    int32_t keep;          /* --keep */
    int32_t force;         /* --force, overwrite outputs, write compressed data to a terminal */
    int32_t recursive;     /* --recursive, descend into directories */
    int32_t verbose;       /* --verbose, report each file processed */
    int32_t quiet;         /* --quiet, suppress warnings */
    int32_t name_mode;     /* -1 default, 0 with --no-name, 1 with --name */
    int32_t time_mode;     /* -1 default, 0 with --no-name or --no-time, 1 with --name or --time */
    int32_t list;          /* --list, list instead of decompressing */
    int32_t test_mode;     /* --test, check integrity without writing */
    int32_t rsyncable;     /* --rsyncable, rsync friendly output */
    int32_t synchronous;   /* --synchronous, fsync outputs before removing inputs */
    int32_t level;         /* -0 to -9 */
    int32_t strategy;      /* deflate strategy options */
    int32_t transparent;   /* -T, copy without compressing */
    int32_t text_mode;     /* -A, no effect where text and binary io agree */
    uint32_t block_size;   /* --blocksize, 0 writes one plain deflate stream */
    int32_t threads;       /* --processes, 0 picks the number of CPUs */
    int32_t threads_given; /* --processes was given, threads of 0 means every CPU, not the default */
} gzng_options;

void gzng_options_init(gzng_options *opt);

/* Preset options from the program name, gunzip, zcat, and gzcat act as gzip's aliases do. */
void gzng_options_personas(gzng_options *opt, const char *argv0);

/* Parse the command line the way gzip does, options and files in any order, short options
   clustered, values attached or separate, -- ending the options. File arguments are compacted
   into argv[1..*nfiles]. Returns 0 to process files, 1 when the run is already complete
   (--help, --version), or -1 on a bad command line with a message printed. */
int32_t gzng_options_parse(gzng_options *opt, int32_t argc, char **argv, int32_t *nfiles);

void gzng_usage(FILE *out);

/* Parse a size with an optional K, M, or G suffix. Returns 0 when it is not usable. */
uint32_t gzng_parse_size(const char *arg);

#ifdef __cplusplus
}
#endif

#endif
