#include <cstdio>

#include <gtest/gtest.h>

#include "options.h"
#include "zlib-ng.h"

namespace {

TEST(options, defaults) {
    gzng_options opt;
    gzng_options_init(&opt);
    EXPECT_EQ(0, opt.decompress);
    EXPECT_EQ(6, opt.level);
    EXPECT_EQ(0u, opt.block_size);
    EXPECT_EQ(0, opt.threads);
}

TEST(options, files_start_after_options) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "one", a2[] = "two";
    char *argv[] = {a0, a1, a2, nullptr};
    EXPECT_EQ(1, gzng_options_parse(&opt, 3, argv));
}

TEST(options, unknown_option_fails) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-x";
    char *argv[] = {a0, a1, nullptr};
    EXPECT_EQ(-1, gzng_options_parse(&opt, 2, argv));
}

TEST(options, decompress_flag) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-d", a2[] = "f";
    char *argv[] = {a0, a1, a2, nullptr};
    EXPECT_EQ(2, gzng_options_parse(&opt, 3, argv));
    EXPECT_EQ(1, opt.decompress);
}

TEST(options, stdout_flag) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-c";
    char *argv[] = {a0, a1, nullptr};
    EXPECT_EQ(2, gzng_options_parse(&opt, 2, argv));
    EXPECT_EQ(1, opt.stdout_mode);
}

TEST(options, personas) {
    gzng_options opt;
    gzng_options_init(&opt);
    gzng_options_personas(&opt, "/usr/bin/gunzip");
    EXPECT_EQ(1, opt.decompress);
    EXPECT_EQ(0, opt.stdout_mode);
    gzng_options_init(&opt);
    gzng_options_personas(&opt, "zcat");
    EXPECT_EQ(1, opt.decompress);
    EXPECT_EQ(1, opt.stdout_mode);
}

TEST(options, keep_flag) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-k";
    char *argv[] = {a0, a1, nullptr};
    EXPECT_EQ(2, gzng_options_parse(&opt, 2, argv));
    EXPECT_EQ(1, opt.keep);
}

TEST(options, levels) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-1";
    char *argv[] = {a0, a1, nullptr};
    EXPECT_EQ(2, gzng_options_parse(&opt, 2, argv));
    EXPECT_EQ(1, opt.level);
    argv[1][1] = '9';
    EXPECT_EQ(2, gzng_options_parse(&opt, 2, argv));
    EXPECT_EQ(9, opt.level);
}

TEST(options, strategies) {
    struct { const char *arg; int strategy; } cases[] = {
        {"-f", Z_FILTERED}, {"-h", Z_HUFFMAN_ONLY}, {"-R", Z_RLE}, {"-F", Z_FIXED}};
    for (auto &c : cases) {
        gzng_options opt;
        gzng_options_init(&opt);
        char a0[] = "gzip-ng";
        char a1[8];
        snprintf(a1, sizeof(a1), "%s", c.arg);
        char *argv[] = {a0, a1, nullptr};
        EXPECT_EQ(2, gzng_options_parse(&opt, 2, argv)) << c.arg;
        EXPECT_EQ(c.strategy, opt.strategy) << c.arg;
    }
}

TEST(options, transparent_and_text) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-T", a2[] = "-A";
    char *argv[] = {a0, a1, a2, nullptr};
    EXPECT_EQ(3, gzng_options_parse(&opt, 3, argv));
    EXPECT_EQ(1, opt.transparent);
    EXPECT_EQ(1, opt.text_mode);
}

TEST(options, parse_size) {
    EXPECT_EQ(131072u, gzng_parse_size("128K"));
    EXPECT_EQ(1u << 20, gzng_parse_size("1M"));
    EXPECT_EQ(0u, gzng_parse_size("1G"));      /* over the engine cap */
    EXPECT_EQ(256u << 20, gzng_parse_size("256M"));
    EXPECT_EQ(4096u, gzng_parse_size("4096"));
    EXPECT_EQ(0u, gzng_parse_size("0"));
    EXPECT_EQ(0u, gzng_parse_size("x"));
    EXPECT_EQ(0u, gzng_parse_size("12KB"));
    EXPECT_EQ(0u, gzng_parse_size("512M"));
}

TEST(options, block_size_flag) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-b", a2[] = "64K";
    char *argv[] = {a0, a1, a2, nullptr};
    EXPECT_EQ(3, gzng_options_parse(&opt, 3, argv));
    EXPECT_EQ(64u * 1024, opt.block_size);
    char bad[] = "junk";
    argv[2] = bad;
    EXPECT_EQ(-1, gzng_options_parse(&opt, 3, argv));
}

TEST(options, threads_flag) {
    gzng_options opt;
    gzng_options_init(&opt);
    char a0[] = "gzip-ng", a1[] = "-p", a2[] = "8";
    char *argv[] = {a0, a1, a2, nullptr};
    EXPECT_EQ(3, gzng_options_parse(&opt, 3, argv));
    EXPECT_EQ(8, opt.threads);
    char bad[] = "-2";
    argv[2] = bad;
    EXPECT_EQ(-1, gzng_options_parse(&opt, 3, argv));
}

}  // namespace
