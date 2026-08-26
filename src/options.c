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
    fprintf(out, "Usage: gzip-ng [--help] [--version] [files...]\n");
    fprintf(out, "Compresses files in place. With no files, compresses stdin to stdout.\n");
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
        fprintf(stderr, "%s: unknown option %s\n", argv[0], arg);
        gzng_usage(stderr);
        return -1;
    }
    return i;
}
