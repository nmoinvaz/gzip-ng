/* gzblock_p.h -- the shared prelude of the reader and writer
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_P_H_
#define GZBLOCK_P_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec.h"
#include "buf.h"
#include "format.h"
#include "gzblock.h"
#include "pipeline.h"
#include "pool.h"
#include "util.h"
#include "zlib-ng.h"

#define IO_CHUNK (256 * 1024) /* read and write in this much at a time */
#define MSG_LEN  128          /* room for one error message */

#endif /* GZBLOCK_P_H_ */
