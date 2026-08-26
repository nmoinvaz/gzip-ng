#include <cstdio>
#include <cstring>

#include <gtest/gtest.h>

#include "options.h"
#include "zlib-ng.h"

namespace {

struct Args {
    char buf[16][32];
    char *argv[17];
    int argc = 0;

    explicit Args(std::initializer_list<const char *> list) {
        snprintf(buf[argc], sizeof(buf[0]), "gzip-ng");
        argv[argc] = buf[argc];
        argc++;
        for (const char *a : list) {
            snprintf(buf[argc], sizeof(buf[0]), "%s", a);
            argv[argc] = buf[argc];
            argc++;
        }
        argv[argc] = nullptr;
    }
};

int parse(gzng_options *opt, Args &a, int *nfiles) {
    gzng_options_init(opt);
    return gzng_options_parse(opt, a.argc, a.argv, nfiles);
}

TEST(options, defaults) {
    gzng_options opt;
    gzng_options_init(&opt);
    EXPECT_EQ(0, opt.decompress);
    EXPECT_EQ(6, opt.level);
    EXPECT_EQ(0u, opt.block_size);
    EXPECT_EQ(0, opt.threads);
}

TEST(options, files_and_options_any_order) {
    gzng_options opt;
    Args a({"one", "-k", "two", "-d"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(2, nfiles);
    EXPECT_STREQ("one", a.argv[1]);
    EXPECT_STREQ("two", a.argv[2]);
    EXPECT_EQ(1, opt.keep);
    EXPECT_EQ(1, opt.decompress);
}

TEST(options, clustered_shorts) {
    gzng_options opt;
    Args a({"-dck9"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.decompress);
    EXPECT_EQ(1, opt.stdout_mode);
    EXPECT_EQ(1, opt.keep);
    EXPECT_EQ(9, opt.level);
}

TEST(options, attached_and_separate_values) {
    gzng_options opt;
    Args a({"-p8", "-b", "64K"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(8, opt.threads);
    EXPECT_EQ(64u * 1024, opt.block_size);
}

TEST(options, long_options) {
    gzng_options opt;
    Args a({"--decompress", "--stdout", "--keep", "--best", "--blocksize", "128K", "--processes", "4"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.decompress);
    EXPECT_EQ(1, opt.stdout_mode);
    EXPECT_EQ(1, opt.keep);
    EXPECT_EQ(9, opt.level);
    EXPECT_EQ(128u * 1024, opt.block_size);
    EXPECT_EQ(4, opt.threads);
}

TEST(options, double_dash_ends_options) {
    gzng_options opt;
    Args a({"--", "-k"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, nfiles);
    EXPECT_STREQ("-k", a.argv[1]);
    EXPECT_EQ(0, opt.keep);
}

TEST(options, dash_is_a_file) {
    gzng_options opt;
    Args a({"-"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, nfiles);
    EXPECT_STREQ("-", a.argv[1]);
}

TEST(options, force_flag) {
    gzng_options opt;
    Args a({"-f"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.force);
    Args b({"--force"});
    EXPECT_EQ(0, parse(&opt, b, &nfiles));
    EXPECT_EQ(1, opt.force);
}

TEST(options, help_short) {
    gzng_options opt;
    Args a({"-h"});
    int nfiles = 0;
    EXPECT_EQ(1, parse(&opt, a, &nfiles));
}

TEST(options, name_modes) {
    gzng_options opt;
    Args a({"-n"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(0, opt.name_mode);
    Args b({"--name"});
    EXPECT_EQ(0, parse(&opt, b, &nfiles));
    EXPECT_EQ(1, opt.name_mode);
}

TEST(options, quiet_flag) {
    gzng_options opt;
    Args a({"-v", "-q"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.quiet);
    EXPECT_EQ(0, opt.verbose);
}

TEST(options, verbose_flag) {
    gzng_options opt;
    Args a({"-v"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.verbose);
}

TEST(options, recursive_flag) {
    gzng_options opt;
    Args a({"-r"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
    EXPECT_EQ(1, opt.recursive);
}

TEST(options, license) {
    gzng_options opt;
    Args a({"-L"});
    int nfiles = 0;
    EXPECT_EQ(1, parse(&opt, a, &nfiles));
}

TEST(options, version_short) {
    gzng_options opt;
    Args a({"-V"});
    int nfiles = 0;
    EXPECT_EQ(1, parse(&opt, a, &nfiles));
}

TEST(options, unknown_option_fails) {
    gzng_options opt;
    Args a({"-x"});
    int nfiles = 0;
    EXPECT_EQ(-1, parse(&opt, a, &nfiles));
    Args b({"--nonsense"});
    EXPECT_EQ(-1, parse(&opt, b, &nfiles));
}

TEST(options, strategies) {
    struct { const char *arg; int strategy; } cases[] = {
        {"--filtered", Z_FILTERED}, {"-H", Z_HUFFMAN_ONLY}, {"-U", Z_RLE}, {"--fixed", Z_FIXED}};
    for (auto &c : cases) {
        gzng_options opt;
        Args a({c.arg});
        int nfiles = 0;
        EXPECT_EQ(0, parse(&opt, a, &nfiles)) << c.arg;
        EXPECT_EQ(c.strategy, opt.strategy) << c.arg;
    }
}

TEST(options, transparent_and_text) {
    gzng_options opt;
    Args a({"-T", "-A"});
    int nfiles = 0;
    EXPECT_EQ(0, parse(&opt, a, &nfiles));
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

}  // namespace
