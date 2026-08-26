#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "gzblock.h"
#include "zlib-ng.h"

namespace {

std::vector<uint8_t> sample_data(size_t len) {
    std::vector<uint8_t> data(len);
    for (size_t i = 0; i < len; i++)
        data[i] = static_cast<uint8_t>("the quick brown fox jumps over the lazy dog "[i % 44] + (i / 8191) % 7);
    return data;
}

size_t vec_write(void *ctx, const uint8_t *buf, size_t len) {
    auto *v = static_cast<std::vector<uint8_t> *>(ctx);
    v->insert(v->end(), buf, buf + len);
    return len;
}

std::vector<uint8_t> block_compress(const std::vector<uint8_t> &data, uint32_t block_size,
                                    int nthreads, size_t chunk = 65521) {
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_wopen(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY,
                                      block_size, nthreads);
    EXPECT_NE(nullptr, w);
    for (size_t pos = 0; pos < data.size(); pos += chunk) {
        size_t n = std::min(chunk, data.size() - pos);
        EXPECT_EQ(0, gzblock_write(w, data.data() + pos, n)) << gzblock_werror(w);
    }
    EXPECT_EQ(0, gzblock_wfinish(w)) << gzblock_werror(w);
    gzblock_wclose(w);
    return out;
}

std::vector<uint8_t> whole_inflate(const std::vector<uint8_t> &packed, size_t expect) {
    std::vector<uint8_t> out(expect + 64);
    zng_stream z;
    memset(&z, 0, sizeof(z));
    EXPECT_EQ(Z_OK, zng_inflateInit2(&z, 15 + 16));
    z.next_in = packed.data();
    z.avail_in = static_cast<uint32_t>(packed.size());
    z.next_out = out.data();
    z.avail_out = static_cast<uint32_t>(out.size());
    EXPECT_EQ(Z_STREAM_END, zng_inflate(&z, Z_FINISH));
    out.resize(z.total_out);
    zng_inflateEnd(&z);
    return out;
}

TEST(block_writer, header_records_block_size) {
    auto data = sample_data(200000);
    auto packed = block_compress(data, 64 * 1024, 1);
    size_t hdr_len = 0;
    uint32_t block_size = 0;
    ASSERT_EQ(1, gzblock_parse_header(packed.data(), packed.size(), &hdr_len, &block_size));
    EXPECT_EQ(64u * 1024, block_size);
    EXPECT_GT(hdr_len, 10u);
}

TEST(block_writer, output_inflates_back) {
    auto data = sample_data(1 << 20);
    auto packed = block_compress(data, 64 * 1024, 1);
    EXPECT_EQ(data, whole_inflate(packed, data.size()));
}

TEST(block_writer, threads_do_not_change_the_bytes) {
    auto data = sample_data(1 << 20);
    auto serial = block_compress(data, 64 * 1024, 1);
    auto threaded = block_compress(data, 64 * 1024, 3);
    EXPECT_EQ(serial, threaded);
}

TEST(block_writer, flush_and_params_inside_a_block) {
    auto data = sample_data(300000);
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_wopen(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY,
                                      64 * 1024, 2);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_write(w, data.data(), 100000)) << gzblock_werror(w);
    ASSERT_EQ(0, gzblock_wflush(w)) << gzblock_werror(w);
    ASSERT_EQ(0, gzblock_wsetparams(w, 1, Z_DEFAULT_STRATEGY)) << gzblock_werror(w);
    ASSERT_EQ(0, gzblock_write(w, data.data() + 100000, data.size() - 100000)) << gzblock_werror(w);
    ASSERT_EQ(0, gzblock_wfinish(w)) << gzblock_werror(w);
    gzblock_wclose(w);
    EXPECT_EQ(data, whole_inflate(out, data.size()));
}

}  // namespace
