/* gtest_block.h -- test data and in-memory streams shared by the reader and writer tests */

#ifndef GZNG_GTEST_BLOCK_H_
#define GZNG_GTEST_BLOCK_H_

#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "gzblock.h"
#include "zlib-ng.h"

inline std::vector<uint8_t> sample_data(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
        data[i] = static_cast<uint8_t>("the quick brown fox jumps over the lazy dog "[i % 44] + (i / 8191) % 7);
    return data;
}

// Aperiodic but compressible bytes. Periodic sample data walks the rolling hash through a tiny
// orbit that can miss the trigger entirely, which is exactly what a boundary test must not do.
inline std::vector<uint8_t> varied_data(size_t len) {
    std::vector<uint8_t> data(len);
    uint32_t s = 0x12345678;
    for (size_t i = 0; i < len; i++) {
        s = s * 1664525u + 1013904223u;
        data[i] = static_cast<uint8_t>(0x20 + ((s >> 24) & 0x3f));
    }
    return data;
}

inline size_t vec_write(void *ctx, const uint8_t *buf, size_t len) {
    auto *v = static_cast<std::vector<uint8_t> *>(ctx);
    v->insert(v->end(), buf, buf + len);
    return len;
}

inline std::vector<uint8_t> block_compress(const std::vector<uint8_t> &data, uint32_t block_size, int nthreads,
                                           size_t chunk = 65521) {
    std::vector<uint8_t> out;
    gzblock_writer *w =
        gzblock_writer_open(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, block_size, nthreads);
    EXPECT_NE(nullptr, w);
    for (size_t pos = 0; pos < data.size(); pos += chunk) {
        size_t n = std::min(chunk, data.size() - pos);
        EXPECT_EQ(0, gzblock_writer_write(w, data.data() + pos, n)) << gzblock_writer_error(w);
    }
    EXPECT_EQ(0, gzblock_writer_finish(w)) << gzblock_writer_error(w);
    gzblock_writer_close(w);
    return out;
}

inline std::vector<uint8_t> whole_inflate(const std::vector<uint8_t> &packed, size_t expect) {
    std::vector<uint8_t> out(expect + 64);
    zng_stream strm;
    memset(&strm, 0, sizeof(strm));
    EXPECT_EQ(Z_OK, zng_inflateInit2(&strm, MAX_WBITS + 16));
    strm.next_in = packed.data();
    strm.avail_in = static_cast<uint32_t>(packed.size());
    strm.next_out = out.data();
    strm.avail_out = static_cast<uint32_t>(out.size());
    EXPECT_EQ(Z_STREAM_END, zng_inflate(&strm, Z_FINISH));
    out.resize(strm.total_out);
    zng_inflateEnd(&strm);
    return out;
}

struct MemIn {
    const uint8_t *p;
    size_t len, pos, chunk;
};

inline size_t mem_read(void *ctx, uint8_t *buf, size_t len) {
    auto *in = static_cast<MemIn *>(ctx);
    size_t n = std::min(std::min(len, in->chunk), in->len - in->pos);
    memcpy(buf, in->p + in->pos, n);
    in->pos += n;
    return n;
}

inline std::vector<uint8_t> block_read(const std::vector<uint8_t> &packed, int nthreads, size_t io_chunk = 65521) {
    MemIn in{packed.data(), packed.size(), 0, io_chunk};
    gzblock_reader *r = gzblock_reader_open(mem_read, &in, nullptr, 0, nthreads);
    EXPECT_NE(nullptr, r);
    std::vector<uint8_t> out;
    uint8_t buf[65521];
    for (;;) {
        size_t got = 0;
        EXPECT_EQ(0, gzblock_reader_read(r, buf, sizeof(buf), &got)) << gzblock_reader_error(r);
        if (got == 0)
            break;
        out.insert(out.end(), buf, buf + got);
    }
    gzblock_reader_close(r);
    return out;
}

#endif /* GZNG_GTEST_BLOCK_H_ */
