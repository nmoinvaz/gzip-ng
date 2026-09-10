#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "format.h"
#include "gtest_block.h"
#include "gzblock.h"
#include "zlib-ng.h"

namespace {

TEST(block_writer, header_is_an_ordinary_gzip_header) {
    auto data = sample_data(200000);
    auto packed = block_compress(data, 64 * 1024, 1);
    /* Nothing marks the member as cut into blocks, a reader finds that out by looking. */
    EXPECT_EQ(10u, format_header_parse(packed.data(), packed.size(), nullptr));
    EXPECT_EQ(0, packed[3] & 4) << "no extra field";
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
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, Z_DEFAULT_COMPRESSION, Z_DEFAULT_STRATEGY, 64 * 1024, 2);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), 100000)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_flush(w)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_setparams(w, 1, Z_DEFAULT_STRATEGY)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_write(w, data.data() + 100000, data.size() - 100000)) << gzblock_writer_error(w);
    ASSERT_EQ(0, gzblock_writer_finish(w)) << gzblock_writer_error(w);
    gzblock_writer_close(w);
    EXPECT_EQ(data, whole_inflate(out, data.size()));
}

TEST(block_writer, meta_lands_in_the_header) {
    auto data = sample_data(1000);
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 64 * 1024, 1);
    ASSERT_NE(nullptr, w);
    ASSERT_EQ(0, gzblock_writer_meta(w, 12345u, "hello.txt"));
    ASSERT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    ASSERT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);
    ASSERT_GT(out.size(), 31u);
    EXPECT_EQ(8, out[3]) << "the name flag, and nothing else";
    EXPECT_EQ(12345u, (uint32_t)out[4] | ((uint32_t)out[5] << 8));
    EXPECT_EQ(0, memcmp(out.data() + 10, "hello.txt", 10)) << "the name follows the fixed bytes";
    EXPECT_EQ(data, block_read(out, 1));
}

std::vector<uint8_t> rsync_compress(const std::vector<uint8_t> &data) {
    std::vector<uint8_t> out;
    gzblock_writer *w = gzblock_writer_open(vec_write, &out, 6, Z_DEFAULT_STRATEGY, 64 * 1024, 1);
    EXPECT_NE(nullptr, w);
    EXPECT_EQ(0, gzblock_writer_rsyncable(w, 1));
    EXPECT_EQ(0, gzblock_writer_write(w, data.data(), data.size()));
    EXPECT_EQ(0, gzblock_writer_finish(w));
    gzblock_writer_close(w);
    return out;
}

TEST(block_writer, rsyncable_realigns_after_an_edit) {
    auto v1 = varied_data(4 << 20);
    std::vector<uint8_t> v2 = v1;
    std::vector<uint8_t> insert(100, 0x55);
    v2.insert(v2.begin() + v2.size() / 4, insert.begin(), insert.end());

    auto p1 = rsync_compress(v1);
    auto p2 = rsync_compress(v2);
    EXPECT_EQ(v1, block_read(p1, 3));
    EXPECT_EQ(v2, block_read(p2, 3));

    /* The 8 byte trailer always differs, crc and size, so compare ahead of it. */
    size_t common = 0, n1 = p1.size() - 8, n2 = p2.size() - 8;
    while (common < n1 && common < n2 && p1[n1 - 1 - common] == p2[n2 - 1 - common])
        common++;
    EXPECT_GT(common, std::min(n1, n2) / 4) << "tails did not re-align";
}

}  // namespace
