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
    fprintf(out, "Usage: gzip-ng [-c] [-d] [-k] [-f|-h|-R|-F|-T] [-A] [-b size] [-p threads] [-0 to -9] [--help] [--version] [files...]\n");
    fprintf(out, "Compresses files in place. With no files, filters stdin to stdout.\n\n");
    fprintf(out, "  -c : write to standard output, keep the files\n");
    fprintf(out, "  -d : decompress\n");
    fprintf(out, "  -k : keep input files\n");
    fprintf(out, "  -0 to -9 : compression level, 6 by default\n");
    fprintf(out, "  -f : filtered strategy, -h : huffman only, -R : run length, -F : fixed codes\n");
    fprintf(out, "  -b size : compress in independent blocks of size, K, M, and G suffixes\n");
    fprintf(out, "  -p threads : threads to use, 0 picks the number of CPUs\n");
    fprintf(out, "  -T : store without compressing\n");
    fprintf(out, "  -A : text mode, accepted for compatibility\n");
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

int gzng_options_parse(gzng_options *opt, int argc, char **argv) {
    int i;

    (void)opt;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] != '-')
            break;
        if (strcmp(arg, "--help") == 0) {
            gzng_usage(stdout);
            return 0;
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            printf("gzip-ng %s (zlib-ng %s)\n", gzng_version(), gzng_zlibng_version());
            return 0;
        }
        if (strcmp(arg, "-c") == 0) {
            opt->stdout_mode = 1;
            continue;
        }
        if (strcmp(arg, "-d") == 0) {
            opt->decompress = 1;
            continue;
        }
        if (strcmp(arg, "-k") == 0) {
            opt->keep = 1;
            continue;
        }
        if (strcmp(arg, "-p") == 0 && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != 0 || n < 0 || n > 1024) {
                fprintf(stderr, "%s: bad thread count %s\n", argv[0], argv[i]);
                return -1;
            }
            opt->threads = (int)n;
            continue;
        }
        if (strcmp(arg, "-p") == 0 && i + 1 < argc) {
            char *end;
            long n = strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != 0 || n < 0 || n > 1024) {
                fprintf(stderr, "%s: bad thread count %s\n", argv[0], argv[i]);
                return -1;
            }
            opt->threads = (int)n;
            continue;
        }
        if (strcmp(arg, "-b") == 0 && i + 1 < argc) {
            opt->block_size = gzng_parse_size(argv[++i]);
            if (opt->block_size == 0) {
                fprintf(stderr, "%s: bad block size %s\n", argv[0], argv[i]);
                return -1;
            }
            continue;
        }
        if (strcmp(arg, "-T") == 0) {
            opt->transparent = 1;
            continue;
        }
        if (strcmp(arg, "-A") == 0) {
            opt->text_mode = 1;
            continue;
        }
        if (strcmp(arg, "-f") == 0 || strcmp(arg, "-h") == 0 || strcmp(arg, "-R") == 0 ||
            strcmp(arg, "-F") == 0) {
            opt->strategy = arg[1] == 'f'   ? Z_FILTERED
                            : arg[1] == 'h' ? Z_HUFFMAN_ONLY
                            : arg[1] == 'R' ? Z_RLE
                                            : Z_FIXED;
            continue;
        }
        if (arg[1] >= '0' && arg[1] <= '9' && arg[2] == 0) {
            opt->level = arg[1] - '0';
            continue;
        }
        fprintf(stderr, "%s: unknown option %s\n", argv[0], arg);
        gzng_usage(stderr);
        return -1;
    }
    return i;
}
