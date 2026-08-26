#include "gzng.h"

#include "zlib-ng.h"

const char *gzng_version(void) {
    return GZNG_VERSION;
}

const char *gzng_zlibng_version(void) {
    return zlibng_version();
}
