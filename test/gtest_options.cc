#include <gtest/gtest.h>

#include "options.h"

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

}  // namespace
