/* main.c -- gzip-ng command line tool
 * For conditions of distribution and use, see LICENSE.md
 */

#include <string.h>

#include "options.h"
#include "process.h"

int main(int argc, char **argv) {
    gzng_options opt;
    int nfiles = 0, rc = 0, ret;

    gzng_options_init(&opt);
    gzng_options_personas(&opt, argv[0]);
    ret = gzng_options_parse(&opt, argc, argv, &nfiles);
    if (ret == 1)
        return 0;
    if (ret < 0)
        return 1;
    if (nfiles == 0)
        return gzng_process_stdio(&opt) != 0 ? 1 : 0;
    for (int i = 1; i <= nfiles; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (gzng_process_stdio(&opt) != 0)
                rc = 1;
        } else if (gzng_process_file(argv[i], &opt) != 0) {
            rc = 1;
        }
    }
    return rc;
}
