#include <stdio.h>
#include <string.h>

#include "gzng.h"

static void usage(FILE *out) {
    fprintf(out, "usage: gzip-ng [-hV]\n");
    fprintf(out, "The engine and gzip flag surface are still being ported, see the readme.\n");
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("gzip-ng %s (zlib-ng %s)\n", gzng_version(), gzng_zlibng_version());
            return 0;
        }
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        fprintf(stderr, "gzip-ng: unknown option %s\n", argv[i]);
        usage(stderr);
        return 1;
    }
    usage(stdout);
    return 0;
}
