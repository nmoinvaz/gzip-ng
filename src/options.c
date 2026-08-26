/* options.c -- gzip-ng command line options
 * For conditions of distribution and use, see LICENSE.md
 */

#include "options.h"

#include <string.h>

#include "gzng.h"
#include "zlib-ng.h"

void gzng_options_init(gzng_options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->level = 6;
    opt->strategy = Z_DEFAULT_STRATEGY;
}

void gzng_usage(FILE *out) {
    fprintf(out, "Usage: gzip-ng [-c] [-d] [-k] [--help] [--version] [files...]\n");
    fprintf(out, "Compresses files in place. With no files, filters stdin to stdout.\n\n");
    fprintf(out, "  -c : write to standard output, keep the files\n");
    fprintf(out, "  -d : decompress\n");
    fprintf(out, "  -k : keep input files\n");
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
        fprintf(stderr, "%s: unknown option %s\n", argv[0], arg);
        gzng_usage(stderr);
        return -1;
    }
    return i;
}
