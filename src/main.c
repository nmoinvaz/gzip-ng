/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include "options.h"
#include "process.h"

int main(int argc, char **argv) {
    gzng_options opt;
    int first, rc = 0;

    gzng_options_init(&opt);
    first = gzng_options_parse(&opt, argc, argv);
    if (first == 0)
        return 0;
    if (first < 0)
        return 64;   /* EX_USAGE */
    if (first == argc)
        return gzng_process_stdio(&opt) != 0 ? 1 : 0;
    for (int i = first; i < argc; i++)
        if (gzng_process_file(argv[i], &opt) != 0)
            rc = 1;
    return rc;
}
