/* options.c -- gzip-ng command line options
 * For conditions of distribution and use, see LICENSE.md
 */

#include "options.h"

#include <stdlib.h>
#include <string.h>

#include "gzblock.h"
#include "gzng.h"
#include "zlib-ng.h"

void gzng_options_init(gzng_options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->level = 6;
    opt->strategy = Z_DEFAULT_STRATEGY;
}

void gzng_usage(FILE *out) {
    fprintf(out, "Usage: gzip-ng [options] [files...]\n");
    fprintf(out, "Compresses files in place. With no files, or -, filters stdin to stdout.\n\n");
    fprintf(out, "  -c --stdout      write to standard output, keep the files\n");
    fprintf(out, "  -d --decompress  decompress\n");
    fprintf(out, "  -k --keep        keep input files\n");
    fprintf(out, "  -r --recursive   descend into directories\n");
    fprintf(out, "  -v --verbose     report each file processed\n");
    fprintf(out, "  -q --quiet       suppress warnings\n");
    fprintf(out, "  -f --force       overwrite outputs, compress to a terminal\n");
    fprintf(out, "  -H --huffman     huffman only strategy, -U --rle run length\n");
    fprintf(out, "     --filtered --fixed   the remaining deflate strategies\n");
    fprintf(out, "  -T : store without compressing\n");
    fprintf(out, "  -A : text mode, accepted for compatibility\n");
    fprintf(out, "  -b --blocksize size   compress in independent blocks, K, M, and G suffixes\n");
    fprintf(out, "  -p --processes n      threads to use, 0 picks the number of CPUs\n");
    fprintf(out, "  -1 --fast .. -9 --best  compression level, 6 by default\n");
    fprintf(out, "  -h --help        show this help\n");
    fprintf(out, "  -V --version     show the version\n");
    fprintf(out, "  -L --license     show the license\n");
}

uint32_t gzng_parse_size(const char *arg) {
    char *end;
    unsigned long long v = strtoull(arg, &end, 10);

    switch (*end) {
    case 'k': case 'K': v <<= 10; end++; break;
    case 'm': case 'M': v <<= 20; end++; break;
    case 'g': case 'G': v <<= 30; end++; break;
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

static void show_license(void) {
    printf("gzip-ng %s, zlib license\n\n", gzng_version());
    printf("This software is provided 'as-is', without any express or implied\n"
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

/* The value for a short option, attached like -p8 or the next argument. NULL when missing. */
static const char *shortval(const char *rest, int argc, char **argv, int *i) {
    if (*rest != 0)
        return rest;
    if (*i + 1 < argc)
        return argv[++*i];
    return NULL;
}

static int set_blocksize(gzng_options *opt, const char *prog, const char *val) {
    if (val == NULL || (opt->block_size = gzng_parse_size(val)) == 0)
        return bad(prog, "bad block size", val ? val : "(missing)");
    return 0;
}

static int set_threads(gzng_options *opt, const char *prog, const char *val) {
    char *end;
    long n = val ? strtol(val, &end, 10) : 0;

    if (val == NULL || end == val || *end != 0 || n < 0 || n > 1024)
        return bad(prog, "bad thread count", val ? val : "(missing)");
    opt->threads = (int)n;
    return 0;
}

static int parse_long(gzng_options *opt, const char *prog, const char *arg,
                      int argc, char **argv, int *i) {
    const char *val;

    if (strcmp(arg, "--stdout") == 0 || strcmp(arg, "--to-stdout") == 0) {
        opt->stdout_mode = 1;
    } else if (strcmp(arg, "--decompress") == 0 || strcmp(arg, "--uncompress") == 0) {
        opt->decompress = 1;
    } else if (strcmp(arg, "--keep") == 0) {
        opt->keep = 1;
    } else if (strcmp(arg, "--force") == 0) {
        opt->force = 1;
    } else if (strcmp(arg, "--recursive") == 0) {
        opt->recursive = 1;
    } else if (strcmp(arg, "--verbose") == 0) {
        opt->verbose = 1;
    } else if (strcmp(arg, "--quiet") == 0) {
        opt->quiet = 1;
        opt->verbose = 0;
    } else if (strcmp(arg, "--filtered") == 0) {
        opt->strategy = Z_FILTERED;
    } else if (strcmp(arg, "--huffman") == 0) {
        opt->strategy = Z_HUFFMAN_ONLY;
    } else if (strcmp(arg, "--rle") == 0) {
        opt->strategy = Z_RLE;
    } else if (strcmp(arg, "--fixed") == 0) {
        opt->strategy = Z_FIXED;
    } else if (strcmp(arg, "--fast") == 0) {
        opt->level = 1;
    } else if (strcmp(arg, "--best") == 0) {
        opt->level = 9;
    } else if (strcmp(arg, "--blocksize") == 0) {
        val = *i + 1 < argc ? argv[++*i] : NULL;
        return set_blocksize(opt, prog, val);
    } else if (strcmp(arg, "--processes") == 0) {
        val = *i + 1 < argc ? argv[++*i] : NULL;
        return set_threads(opt, prog, val);
    } else if (strcmp(arg, "--help") == 0) {
        gzng_usage(stdout);
        return 1;
    } else if (strcmp(arg, "--license") == 0) {
        show_license();
        return 1;
    } else if (strcmp(arg, "--version") == 0) {
        printf("gzip-ng %s (zlib-ng %s)\n", gzng_version(), gzng_zlibng_version());
        return 1;
    } else {
        return bad(prog, "unknown option", arg);
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
            if (rc != 0)
                return rc;
            continue;
        }
        for (int j = 1; arg[j] != 0; j++) {
            switch (arg[j]) {
            case 'c': opt->stdout_mode = 1; break;
            case 'd': opt->decompress = 1; break;
            case 'k': opt->keep = 1; break;
            case 'f': opt->force = 1; break;
            case 'r': opt->recursive = 1; break;
            case 'v': opt->verbose = 1; break;
            case 'q': opt->quiet = 1; opt->verbose = 0; break;
            case 'T': opt->transparent = 1; break;
            case 'A': opt->text_mode = 1; break;
            case 'h':
                gzng_usage(stdout);
                return 1;
            case 'L':
                show_license();
                return 1;
            case 'V':
                printf("gzip-ng %s (zlib-ng %s)\n", gzng_version(), gzng_zlibng_version());
                return 1;
            case 'H': opt->strategy = Z_HUFFMAN_ONLY; break;
            case 'U': opt->strategy = Z_RLE; break;
            case 'b':
                if (set_blocksize(opt, prog, shortval(arg + j + 1, argc, argv, &i)) != 0)
                    return -1;
                j = (int)strlen(arg) - 1;
                break;
            case 'p':
                if (set_threads(opt, prog, shortval(arg + j + 1, argc, argv, &i)) != 0)
                    return -1;
                j = (int)strlen(arg) - 1;
                break;
            default:
                if (arg[j] >= '0' && arg[j] <= '9') {
                    opt->level = arg[j] - '0';
                    break;
                }
                {
                    char unk[3] = {'-', arg[j], 0};
                    return bad(prog, "unknown option", unk);
                }
            }
        }
    }
    *nfiles = nf;
    return 0;
}
