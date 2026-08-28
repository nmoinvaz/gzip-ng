#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "format.h"
#include "util.h"

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
    h.mtime = 0x5f6a7b8c;
    h.name = "some/file.txt";

    size_t n = format_header_build(buf, &h);
    ASSERT_LE(n, sizeof(buf));
    EXPECT_EQ(n, format_header_parse(buf, n, nullptr));
    EXPECT_EQ(0x5f6a7b8cu,
              (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24));
    EXPECT_STREQ("some/file.txt", reinterpret_cast<const char *>(buf + n - 14));
}

TEST(format, a_bare_header_is_ten_bytes) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header h = basic();

    size_t n = format_header_build(buf, &h);
    EXPECT_EQ(10u, n);
    EXPECT_EQ(0, buf[3]) << "no flags, so nothing follows the fixed bytes";
    EXPECT_EQ(n, format_header_parse(buf, n, nullptr));
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
    h.name = "f";
    size_t n = format_header_build(buf, &h);

    for (size_t partial = 1; partial < n; partial++)
        EXPECT_EQ(0u, format_header_parse(buf, partial, nullptr)) << partial;
    EXPECT_EQ(n, format_header_parse(buf, n, nullptr));
}

TEST(format, an_extra_field_is_walked_over) {
    /* Someone else's subfield, which a header may carry and this one has no use for. */
    const uint8_t withextra[] = {0x1f, 0x8b, 8, 4, 0, 0, 0, 0, 0, 3, 6, 0, 'B', 'C', 2, 0, 0x11, 0x22};
    EXPECT_EQ(sizeof(withextra), format_header_parse(withextra, sizeof(withextra), nullptr));
}

TEST(format, parse_reports_the_time_and_name) {
    uint8_t buf[FORMAT_HEADER_MAX];
    format_header h = basic(), got;
    h.mtime = 0x5f6a7b8c;
    h.name = "some/file.txt";

    size_t n = format_header_build(buf, &h);
    EXPECT_EQ(n, format_header_parse(buf, n, &got));
    EXPECT_EQ(0x5f6a7b8cu, got.mtime);
    EXPECT_STREQ("some/file.txt", got.name);
    /* The fixed bytes give up the time before the name has fully arrived. */
    EXPECT_EQ(0u, format_header_parse(buf, n - 1, &got));
    EXPECT_EQ(0x5f6a7b8cu, got.mtime);
    EXPECT_EQ(nullptr, got.name);
}

TEST(format, two_bytes_decide_the_magic) {
    const uint8_t gz[2] = {0x1f, 0x8b};
    const uint8_t other[2] = {'n', 'o'};
    EXPECT_TRUE(format_is_gzip(gz, sizeof(gz)));
    EXPECT_FALSE(format_is_gzip(other, sizeof(other)));
    EXPECT_FALSE(format_is_gzip(gz, 1)) << "one byte cannot say";
}

TEST(format, other_data_is_rejected) {
    const uint8_t junk[16] = {'n', 'o', 't', ' ', 'g', 'z', 'i', 'p', 0, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_EQ((size_t)-1, format_header_parse(junk, sizeof(junk), nullptr));
}

TEST(format, values_are_stored_little_endian) {
    /* The order is the format's, not the machine's, so it is spelled out here. */
    uint8_t buf[4] = {0, 0, 0, 0};

    store_le32(buf, 0x11223344u);
    EXPECT_EQ(0x44, buf[0]);
    EXPECT_EQ(0x33, buf[1]);
    EXPECT_EQ(0x22, buf[2]);
    EXPECT_EQ(0x11, buf[3]);
    EXPECT_EQ(0x11223344u, load_le32(buf));

    store_le16(buf, 0xabcdu);
    EXPECT_EQ(0xcd, buf[0]);
    EXPECT_EQ(0xab, buf[1]);
    EXPECT_EQ(0xabcdu, load_le16(buf));
}

TEST(format, trailer_round_trips) {
    uint8_t buf[GZ_TRAILER];
    uint32_t crc = 0, total = 0;

    format_trailer_build(buf, 0xdeadbeefu, 0x100000007ull);
    format_trailer_parse(buf, &crc, &total);
    EXPECT_EQ(0xdeadbeefu, crc);
    EXPECT_EQ(7u, total) << "the length is recorded modulo 2^32";
}

}  // namespace
