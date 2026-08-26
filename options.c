/* options.c -- gzip-ng command line options
 * For conditions of distribution and use, see LICENSE.md
 */

#include "options.h"

#include <stdlib.h>
#include <string.h>

#include "gzblock.h"
#include "gzng.h"
#include "util.h"
#include "zlib-ng.h"

void gzng_options_init(gzng_options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->level = 6;
    opt->strategy = Z_DEFAULT_STRATEGY;
    opt->name_mode = -1;
    opt->time_mode = -1;
}

uint32_t gzng_parse_size(const char *arg) {
    char *end;
    unsigned long long v = strtoull(arg, &end, 10);

    switch (*end) {
    case 'k':
    case 'K':
        v <<= 10;
        end++;
        break;
    case 'm':
    case 'M':
        v <<= 20;
        end++;
        break;
    case 'g':
    case 'G':
        v <<= 30;
        end++;
        break;
    }
    if (end == arg || *end != 0 || v == 0 || v > GZBLOCK_MAX_BLOCK)
        return 0;
    return (uint32_t)v;
}

void gzng_options_personas(gzng_options *opt, const char *argv0) {
    const char *base = strrchr(argv0, '/');

    base = base ? base + 1 : argv0;
    if (strcmp(base, "gunzip") == 0) {
        opt->decompress = 1;
    } else if (strcmp(base, "zcat") == 0 || strcmp(base, "gzcat") == 0) {
        opt->decompress = 1;
        opt->stdout_mode = 1;
    }
}

/* ===========================================================================
 * The options, each described once
 * =========================================================================== */

enum {
    OPT_STDOUT = 1,
    OPT_DECOMPRESS,
    OPT_FORCE,
    OPT_KEEP,
    OPT_RECURSIVE,
    OPT_VERBOSE,
    OPT_QUIET,
    OPT_NO_NAME,
    OPT_NAME,
    OPT_NO_TIME,
    OPT_TIME,
    OPT_LIST,
    OPT_TEST,
    OPT_RSYNCABLE,
    OPT_SYNCHRONOUS,
    OPT_HUFFMAN,
    OPT_RLE,
    OPT_FILTERED,
    OPT_FIXED,
    OPT_TRANSPARENT,
    OPT_ASCII,
    OPT_BLOCKSIZE,
    OPT_PROCESSES,
    OPT_FAST,
    OPT_BEST,
    OPT_HELP,
    OPT_VERSION,
    OPT_LICENSE
};

/* One row per option, in the order the help lists them. A row carries every spelling the option
   answers to, so a spelling cannot exist in the parser without appearing in the help. */
typedef struct {
    int id;
    char letter;       /* 0 when there is no short form */
    const char *name;  /* long form without the dashes, NULL when there is none */
    const char *alias; /* second long spelling, or NULL */
    const char *value; /* what the value is called, NULL when the option takes none */
    const char *help;
} option_desc;

static const option_desc option_table[] = {
    {     OPT_STDOUT, 'c',      "stdout",  "to-stdout",   NULL,              "write to standard output, keep the files"},
    { OPT_DECOMPRESS, 'd',  "decompress", "uncompress",   NULL,                                            "decompress"},
    {      OPT_FORCE, 'f',       "force",         NULL,   NULL,             "overwrite outputs, compress to a terminal"},
    {       OPT_KEEP, 'k',        "keep",         NULL,   NULL,                                      "keep input files"},
    {  OPT_RECURSIVE, 'r',   "recursive",         NULL,   NULL,                              "descend into directories"},
    {    OPT_VERBOSE, 'v',     "verbose",         NULL,   NULL,                            "report each file processed"},
    {      OPT_QUIET, 'q',       "quiet",         NULL,   NULL,                                     "suppress warnings"},
    {    OPT_NO_NAME, 'n',     "no-name",         NULL,   NULL,                  "do not save or restore name and time"},
    {       OPT_NAME, 'N',        "name",         NULL,   NULL,                        "save and restore name and time"},
    {    OPT_NO_TIME, 'm',     "no-time",         NULL,   NULL,                       "do not save or restore the time"},
    {       OPT_TIME, 'M',        "time",         NULL,   NULL,                             "save and restore the time"},
    {       OPT_LIST, 'l',        "list",         NULL,   NULL,                         "list compressed file contents"},
    {       OPT_TEST, 't',        "test",         NULL,   NULL,                       "check integrity without writing"},
    {  OPT_RSYNCABLE,   0,   "rsyncable",         NULL,   NULL,                        "make the output rsync friendly"},
    {OPT_SYNCHRONOUS,   0, "synchronous",         NULL,   NULL, "write the output to storage before removing the input"},
    {    OPT_HUFFMAN, 'H',     "huffman",         NULL,   NULL,                                 "huffman only strategy"},
    {        OPT_RLE, 'U',         "rle",         NULL,   NULL,                                   "run length strategy"},
    {   OPT_FILTERED,   0,    "filtered",         NULL,   NULL,                                     "filtered strategy"},
    {      OPT_FIXED,   0,       "fixed",         NULL,   NULL,                                  "fixed codes strategy"},
    {OPT_TRANSPARENT, 'T',          NULL,         NULL,   NULL,                             "store without compressing"},
    {      OPT_ASCII, 'A',          NULL,         NULL,   NULL,                 "text mode, accepted for compatibility"},
    {  OPT_BLOCKSIZE, 'b',   "blocksize",         NULL, "size",  "compress in independent blocks, K, M, and G suffixes"},
    {  OPT_PROCESSES, 'p',   "processes",         NULL,    "n",            "threads to use, 0 picks the number of CPUs"},
    {       OPT_FAST,   0,        "fast",         NULL,   NULL,                              "compress faster, level 1"},
    {       OPT_BEST,   0,        "best",         NULL,   NULL,                              "compress better, level 9"},
    {       OPT_HELP, 'h',        "help",         NULL,   NULL,                                        "show this help"},
    {    OPT_VERSION, 'V',     "version",         NULL,   NULL,                                      "show the version"},
    {    OPT_LICENSE, 'L',     "license",         NULL,   NULL,                                      "show the license"},
};

#define OPTION_COUNT (sizeof(option_table) / sizeof(option_table[0]))

/* The left column of a help line, "-c --stdout" or "   --rsyncable" or "-b --blocksize size". */
static void spellings(const option_desc *opt, char *buf, size_t cap) {
    size_t n = 0;

    if (opt->letter != 0)
        n += (size_t)snprintf(buf + n, cap - n, "-%c", opt->letter);
    else
        n += (size_t)snprintf(buf + n, cap - n, "  ");
    if (opt->name != NULL)
        n += (size_t)snprintf(buf + n, cap - n, " --%s", opt->name);
    if (opt->value != NULL)
        snprintf(buf + n, cap - n, " %s", opt->value);
}

void gzng_usage(FILE *out) {
    char buf[64];
    size_t i, width = 0;

    fprintf(out, "Usage: gzip-ng [options] [files...]\n");
    fprintf(out, "Compresses files in place. With no files, or -, filters stdin to stdout.\n\n");
    for (i = 0; i < OPTION_COUNT; i++) {
        spellings(&option_table[i], buf, sizeof(buf));
        width = MAX(width, strlen(buf));
    }
    for (i = 0; i < OPTION_COUNT; i++) {
        spellings(&option_table[i], buf, sizeof(buf));
        fprintf(out, "  %-*s  %s\n", (int)width, buf, option_table[i].help);
    }
    fprintf(out, "  %-*s  %s\n", (int)width, "-1 .. -9", "compression level, 6 by default");
}

static const option_desc *find_letter(char letter) {
    size_t i;

    for (i = 0; i < OPTION_COUNT; i++)
        if (option_table[i].letter == letter)
            return &option_table[i];
    return NULL;
}

static const option_desc *find_name(const char *name) {
    size_t i;

    for (i = 0; i < OPTION_COUNT; i++) {
        const option_desc *opt = &option_table[i];
        if ((opt->name != NULL && strcmp(opt->name, name) == 0) ||
            (opt->alias != NULL && strcmp(opt->alias, name) == 0))
            return opt;
    }
    return NULL;
}

/* ===========================================================================
 * Parsing
 * =========================================================================== */

static void show_license(void) {
    printf("gzip-ng %s, zlib license\n\n", gzng_version());
    printf(
        "This software is provided 'as-is', without any express or implied\n"
        "warranty. In no event will the authors be held liable for any damages\n"
        "arising from the use of this software.\n\n"
        "Permission is granted to anyone to use this software for any purpose,\n"
        "including commercial applications, and to alter it and redistribute it\n"
        "freely, subject to the restrictions in LICENSE.md.\n");
}

static int bad(const char *prog, const char *what, const char *arg) {
    fprintf(stderr, "%s: %s %s\n", prog, what, arg);
    fprintf(stderr, "Try %s --help for options.\n", prog);
    return -1;
}

/* Carry out one option. Returns 0 to keep parsing, 1 when the run is already complete, -1 on a
   bad value with the message printed. */
static int apply(gzng_options *opt, const option_desc *desc, const char *value, const char *prog) {
    switch (desc->id) {
    case OPT_STDOUT:
        opt->stdout_mode = 1;
        break;
    case OPT_DECOMPRESS:
        opt->decompress = 1;
        break;
    case OPT_FORCE:
        opt->force = 1;
        break;
    case OPT_KEEP:
        opt->keep = 1;
        break;
    case OPT_RECURSIVE:
        opt->recursive = 1;
        break;
    case OPT_VERBOSE:
        opt->verbose = 1;
        break;
    case OPT_QUIET:
        opt->quiet = 1;
        opt->verbose = 0;
        break;
    case OPT_NO_NAME:
        opt->name_mode = 0;
        opt->time_mode = 0;
        break;
    case OPT_NAME:
        opt->name_mode = 1;
        opt->time_mode = 1;
        break;
    case OPT_NO_TIME:
        opt->time_mode = 0;
        break;
    case OPT_TIME:
        opt->time_mode = 1;
        break;
    case OPT_LIST:
        opt->list = 1;
        break;
    case OPT_TEST:
        opt->test_mode = 1;
        opt->decompress = 1;
        break;
    case OPT_RSYNCABLE:
        opt->rsyncable = 1;
        break;
    case OPT_SYNCHRONOUS:
        opt->synchronous = 1;
        break;
    case OPT_HUFFMAN:
        opt->strategy = Z_HUFFMAN_ONLY;
        break;
    case OPT_RLE:
        opt->strategy = Z_RLE;
        break;
    case OPT_FILTERED:
        opt->strategy = Z_FILTERED;
        break;
    case OPT_FIXED:
        opt->strategy = Z_FIXED;
        break;
    case OPT_TRANSPARENT:
        opt->transparent = 1;
        break;
    case OPT_ASCII:
        opt->text_mode = 1;
        break;
    case OPT_FAST:
        opt->level = 1;
        break;
    case OPT_BEST:
        opt->level = 9;
        break;
    case OPT_BLOCKSIZE:
        if (value == NULL || (opt->block_size = gzng_parse_size(value)) == 0)
            return bad(prog, "bad block size", value ? value : "(missing)");
        break;
    case OPT_PROCESSES: {
        char *end;
        long n = value != NULL ? strtol(value, &end, 10) : 0;
        if (value == NULL || end == value || *end != 0 || n < 0 || n > 1024)
            return bad(prog, "bad thread count", value ? value : "(missing)");
        opt->threads = (int)n;
        break;
    }
    case OPT_HELP:
        gzng_usage(stdout);
        return 1;
    case OPT_LICENSE:
        show_license();
        return 1;
    case OPT_VERSION:
        printf("gzip-ng %s (zlib-ng %s)\n", gzng_version(), gzng_zlibng_version());
        return 1;
    }
    return 0;
}

/* A long option, its value taken from the next argument when it wants one. */
static int parse_long(gzng_options *opt, const char *prog, const char *arg, int argc, char **argv, int *i) {
    const option_desc *desc = find_name(arg + 2);
    const char *value = NULL;

    if (desc == NULL)
        return bad(prog, "unknown option", arg);
    if (desc->value != NULL)
        value = *i + 1 < argc ? argv[++*i] : NULL;
    return apply(opt, desc, value, prog);
}

/* A run of short options, any one of which may take the rest of the argument or the next one as
   its value, which ends the run. */
static int parse_shorts(gzng_options *opt, const char *prog, const char *arg, int argc, char **argv, int *i) {
    int j, rc;

    for (j = 1; arg[j] != 0; j++) {
        const option_desc *desc;
        const char *value = NULL;

        if (arg[j] >= '0' && arg[j] <= '9') {
            opt->level = arg[j] - '0';
            continue;
        }
        desc = find_letter(arg[j]);
        if (desc == NULL) {
            char unknown[3] = {'-', arg[j], 0};
            return bad(prog, "unknown option", unknown);
        }
        if (desc->value != NULL) {
            if (arg[j + 1] != 0)
                value = arg + j + 1;
            else if (*i + 1 < argc)
                value = argv[++*i];
            rc = apply(opt, desc, value, prog);
            return rc != 0 ? rc : 0;
        }
        rc = apply(opt, desc, NULL, prog);
        if (rc != 0)
            return rc;
    }
    return 0;
}

int gzng_options_parse(gzng_options *opt, int argc, char **argv, int *nfiles) {
    const char *prog = argv[0];
    int nf = 0, no_more = 0, rc;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (no_more || arg[0] != '-' || arg[1] == 0) {
            argv[1 + nf++] = argv[i];
            continue;
        }
        if (arg[1] == '-') {
            if (arg[2] == 0) {
                no_more = 1;
                continue;
            }
            rc = parse_long(opt, prog, arg, argc, argv, &i);
        } else {
            rc = parse_shorts(opt, prog, arg, argc, argv, &i);
        }
        if (rc != 0)
            return rc;
    }
    *nfiles = nf;
    return 0;
}
