/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include "options.h"

int main(int argc, char **argv) {
    gzng_options opt;
    int first = gzng_options_parse((gzng_options_init(&opt), &opt), argc, argv);

    if (first == 0)
        return 0;
    if (first < 0)
        return 64;   /* EX_USAGE */
    (void)first;
    fprintf(stderr, "gzip-ng: no processing yet, it arrives commit by commit\n");
    return 1;
}
