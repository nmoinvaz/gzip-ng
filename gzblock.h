/* gzblock.h -- gzip members made of independent deflate blocks, written and read in parallel
 * For conditions of distribution and use, see LICENSE.md
 */

#ifndef GZBLOCK_H_
#define GZBLOCK_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One ordinary gzip member whose deflate stream is cut into independent blocks of block_size
   input bytes. Each block ends with two empty stored blocks, the nine bytes
   00 00 FF FF 00 00 00 FF FF, the same shape pigz --independent writes, so a block inflates on
   its own and a boundary is hard to fake. The gzip header records the layout in an extra
   subfield with the ID "ZB", see format.h, whose flags say the boundaries are marker pairs, which
   lets the reader ignore the single markers a flush inside a block or a chance pattern in stored
   data produce. Any deflate stream built the same way decodes here, pigz -i output included, and
   streams with single full flush markers are read by scanning for those.

   A pool of workers runs deflate or inflate over a ring of slots, filled in order and drained in
   order, so output order is slot order and memory is bounded by the ring. Errors are reported
   with a zlib return code and a message. */

/* Largest block size accepted, from a caller or from a file's header. Bounds what a member can make
   the reader allocate, two slots of input and output at this size stay within the ring budget. */
#define GZBLOCK_MAX_BLOCK (256u << 20)

/* Longest file name stored in or read from a header, including the terminator. */
#define GZBLOCK_NAME_MAX 256

/* I/O callbacks. read returns the bytes read, 0 at end of input, (size_t)-1 on error. write returns
   the bytes written, anything short of len is an error. */
typedef size_t (*gzblock_read_fn)(void *ctx, uint8_t *buf, size_t len);
typedef size_t (*gzblock_write_fn)(void *ctx, const uint8_t *buf, size_t len);

/* Writer. Produces one gzip member whose deflate stream is cut into independent blocks of
   block_size input bytes, deflated on nthreads threads, with the block size recorded in a "ZB"
   header extra subfield. nthreads of 0 picks the number of CPUs, 1 does the work on the calling
   thread. */
typedef struct gzblock_writer_s gzblock_writer;

gzblock_writer *gzblock_writer_open(gzblock_write_fn write, void *ctx, int level, int strategy, uint32_t block_size,
                                    int nthreads);
/* End blocks at rolling hash hits after half fill, content-defined boundaries for rsync,
   before the first write. The reader needs nothing special, pairs already carry any size. */
int gzblock_writer_rsyncable(gzblock_writer *w, int on);

/* Record a modification time and file name for the header, before the first write. */
int gzblock_writer_meta(gzblock_writer *w, uint32_t mtime, const char *name);
/* 0, or -1 on error. */
int gzblock_writer_write(gzblock_writer *w, const uint8_t *buf, size_t len);
/* Settings for the blocks to come. */
int gzblock_writer_setparams(gzblock_writer *w, int level, int strategy);
/* End the current block early and write everything out. */
int gzblock_writer_flush(gzblock_writer *w);
/* Write the last block and the trailer. */
int gzblock_writer_finish(gzblock_writer *w);
const char *gzblock_writer_error(const gzblock_writer *w);
/* Z_ERRNO, Z_MEM_ERROR, and the rest. */
int gzblock_writer_errcode(const gzblock_writer *w);
/* Free, without finishing if that has not happened. */
void gzblock_writer_close(gzblock_writer *w);

/* Reader. Decodes gzip data, member by member. A member whose header records a block size, or
   any member when block_size is nonzero, is inflated as independent blocks on nthreads threads at
   once, nthreads of 0 picking the number of CPUs and 1 doing the work on the calling thread.
   Other members are streamed through plain inflate. Input that is not gzip at all is passed
   through unchanged and trailing garbage after the last member is ignored.
   head holds bytes already taken from the input that come before what read() returns, or NULL. */
typedef struct gzblock_reader_s gzblock_reader;

gzblock_reader *gzblock_reader_open(gzblock_read_fn read, void *ctx, const uint8_t *head, size_t head_len,
                                    uint32_t block_size, int nthreads);
/* 0, or -1 on error. */
int gzblock_reader_read(gzblock_reader *r, uint8_t *buf, size_t len, size_t *got);
/* Hand out the next piece of output without copying. *p and *n describe bytes owned by the reader,
   valid until the next read or next call, *n is 0 at the end of the data. 0, or -1 on error. */
int gzblock_reader_next(gzblock_reader *r, const uint8_t **p, size_t *n);
const char *gzblock_reader_error(const gzblock_reader *r);
/* Z_ERRNO, Z_DATA_ERROR, Z_BUF_ERROR, and the rest. */
int gzblock_reader_errcode(const gzblock_reader *r);
void gzblock_reader_close(gzblock_reader *r);

#ifdef __cplusplus
}
#endif

#endif /* GZBLOCK_H_ */
