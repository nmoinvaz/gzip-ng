/* cutter.h -- cutting candidate segments out of the compressed input
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZNG_CUTTER_H_
#define GZNG_CUTTER_H_

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* How a cut ended. FOUND put a segment in seg, DONE is the input used up at its end, MORE wants
   input fed before deciding, TOO_LARGE held max_seg without finding a boundary, ERROR is out of
   memory. */
enum { CUT_ERROR = -1, CUT_TOO_LARGE = -2, CUT_DONE = 0, CUT_FOUND = 1, CUT_MORE = 2 };

typedef struct {
    uint32_t block_size; /* the coalescing target */
    int32_t paired;      /* boundaries are marker pairs, lone markers are not candidates */
    int32_t pair_seen;   /* a pair turned up in this member, so treat it as pair-delimited */
    size_t max_seg;      /* how much input to hold looking for one boundary */
    size_t scanned;      /* bytes of the input already scanned for markers */
    size_t coal;         /* rightmost pair end while coalescing small chunks, 0 when not */
    buf_t seg;           /* segment most recently cut */
    int32_t seg_last;    /* the segment ends the input */
    int32_t seg_pair;    /* the segment ends with a marker pair */
} cutter_t;

/* Set up for one member. The segment buffer carries over from the last one. */
void cutter_init(cutter_t *cutter, uint32_t block_size, int32_t paired);

/* Cut the next candidate segment out of b into seg and say how it went. eof means b holds all the
   input there is. */
int32_t cutter_next(cutter_t *cutter, buf_t *b, int32_t eof);

/* Scanning starts over from byte `from` of the input, after the caller rebuilt it. */
void cutter_rescan(cutter_t *cutter, size_t from);

void cutter_free(cutter_t *cutter);

#ifdef __cplusplus
}
#endif

#endif /* GZNG_CUTTER_H_ */
