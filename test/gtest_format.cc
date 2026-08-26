#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format.h"

namespace {

format_header basic() {
    format_header h;
    memset(&h, 0, sizeof(h));
    h.level = 6;
    return h;
}

TEST(format, build_parses_back) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header h = basic();
    h.block_size = 128 * 1024;
    h.zb_flags = ZB_PAIRED;
    h.mtime = 0x5f6a7b8c;
    h.name = "some/file.txt";

    size_t n = format_header_build(buf, &h);
    ASSERT_LE(n, sizeof(buf));

    uint32_t block_size = 0, zb_flags = 0;
    EXPECT_EQ(n, format_header_parse(buf, n, &block_size, &zb_flags));
    EXPECT_EQ(128u * 1024, block_size);
    EXPECT_EQ(static_cast<uint32_t>(ZB_PAIRED), zb_flags);
    EXPECT_EQ(0x5f6a7b8cu, (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                            ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24));
    EXPECT_STREQ("some/file.txt", reinterpret_cast<const char *>(buf + n - 14));
}

TEST(format, without_a_block_size_there_is_no_subfield) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header h = basic();

    size_t n = format_header_build(buf, &h);
    EXPECT_EQ(10u, n);
    EXPECT_EQ(0, buf[3]);

    uint32_t block_size = 1, zb_flags = 1;
    EXPECT_EQ(n, format_header_parse(buf, n, &block_size, &zb_flags));
    EXPECT_EQ(0u, block_size);
    EXPECT_EQ(0u, zb_flags);
}

TEST(format, a_name_too_long_is_left_out) {
    uint8_t buf[FORMAT_HEADER_MAX];
    std::string huge(GZBLOCK_NAME_MAX + 10, 'x');
    format_header h = basic();
    h.name = huge.c_str();

    size_t n = format_header_build(buf, &h);
    EXPECT_EQ(10u, n);
    EXPECT_EQ(0, buf[3] & 8);
}

TEST(format, partial_headers_ask_for_more) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header h = basic();
    h.block_size = 64 * 1024;
    h.name = "f";
    size_t n = format_header_build(buf, &h);

    uint32_t block_size = 0, zb_flags = 0;
    for (size_t partial = 1; partial < n; partial++)
        EXPECT_EQ(0u, format_header_parse(buf, partial, &block_size, &zb_flags)) << partial;
    EXPECT_EQ(n, format_header_parse(buf, n, &block_size, &zb_flags));
}

TEST(format, other_data_is_rejected) {
    uint32_t block_size = 0, zb_flags = 0;
    const uint8_t junk[16] = {'n', 'o', 't', ' ', 'g', 'z', 'i', 'p', 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ((size_t)-1, format_header_parse(junk, sizeof(junk), &block_size, &zb_flags));
}

}  // namespace
