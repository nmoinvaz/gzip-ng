/* gzng.c -- gzip-ng version strings
 * For conditions of distribution and use, see LICENSE.md
 */

#include "gzng.h"

#include "zlib-ng.h"

const char *gzng_version(void) {
    return GZNG_VERSION;
}

const char *gzng_zlibng_version(void) {
    return zlibng_version();
}
