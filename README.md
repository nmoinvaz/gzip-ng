# gzip-ng

A gzip replacement built on [zlib-ng](https://github.com/zlib-ng/zlib-ng), with parallel compression into independent deflate blocks and parallel decompression of block-structured files, including pigz -i and pigz --rsyncable -i output.

The block engine is ported and the CLI covers the minigzip switch surface, the gzip drop-in surface comes next.

## Goals

- Drop-in for GNU and BSD gzip, flag for flag, distro-safe defaults.
- Decompression parallel by default, output byte-identical at any thread count.
- Compression serial by default, parallel independent blocks opt-in.
- zlib license.

## Build

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

zlib-ng and Google Test are fetched automatically when not found.

## Build Options

| CMake                 | Description                                                | Default |
|:----------------------|:-----------------------------------------------------------|---------|
| GZNG_ENABLE_TESTS     | Build the test binaries and register them with ctest        | ON      |
| GZNG_ENABLE_BENCHMARKS| Build the benchmarks using Google Benchmark                 | OFF     |
| GZNG_THREADS          | Compress and decompress blocks on a thread pool             | ON      |
| GZNG_SIMD             | Scan for block boundaries with NEON or SSE2                 | ON      |

Dependencies are found on the system first and fetched only when missing. Each fetch takes a tag,
so a build can pin what it compiles against.

| CMake            | Description                                     | Default  |
|:-----------------|:------------------------------------------------|----------|
| ZLIBNG_TAG       | Tag of zlib-ng to fetch when none is installed   | 2.3.3    |
| GTEST_TAG        | Tag of Google Test to fetch when none is found   | v1.18.0  |
| GBENCHMARK_TAG   | Tag of Google Benchmark to fetch when none found | v1.9.5   |

## Usage

```
gzip-ng [options] [files...]
```

Compresses files in place, file to file.gz, removing the input unless kept. With no files, or -, it filters stdin to stdout. Options and files mix in any order, short options cluster, and installed as gunzip, zcat, or gzcat it presets -d and -c the way gzip's aliases do.

| Option | Description |
|:-------|:------------|
| `-c` `--stdout` | Write to standard output, keep the files |
| `-d` `--decompress` | Decompress, in parallel for block or pigz -i style input |
| `-f` `--force` | Overwrite outputs, compress to a terminal |
| `-k` `--keep` | Keep input files |
| `-r` `--recursive` | Descend into directories |
| `-t` `--test` | Check integrity without writing |
| `-l` `--list` | List compressed file contents, `-v` adds method, crc, and date |
| `-n` `--no-name` | Do not save or restore the name and time |
| `-N` `--name` | Save and restore the name and time |
| `-v` `--verbose` | Report each file processed |
| `-q` `--quiet` | Suppress warnings |
| `-1` .. `-9` | Compression level, 6 by default |
| `--fast` `--best` | Level 1 and level 9 |
| `-b` `--blocksize` *size* | Compress in independent blocks, K, M, and G suffixes |
| `-p` `--processes` *n* | Threads to use, 0 picks the number of CPUs |
| `-H` `--huffman` | Huffman only strategy |
| `-U` `--rle` | Run length strategy |
| `--filtered` | Filtered strategy |
| `--fixed` | Fixed codes strategy |
| `-T` | Store without compressing |
| `-A` | Text mode, accepted for compatibility |
| `--rsyncable` | Content-defined block ends, so edits stay local for rsync |
| `--synchronous` | Write outputs to permanent storage before removing inputs |
| `-h` `--help` | Show the usage summary |
| `-V` `--version` | Show the version |
| `-L` `--license` | Show the license |

Exit status is 0, 1 on errors, 2 on warnings, as gzip behaves.

## Benchmarks

Engine benchmarks use Google Benchmark, off by default:

```
cmake -B build-bench -DCMAKE_BUILD_TYPE=Release -DGZNG_ENABLE_BENCHMARKS=ON
cmake --build build-bench
build-bench/test/benchmarks/benchmark_gzng
```

Comparing whole binaries is a different job, fork and exec noise belongs to tools built for it. `test/benchmarks/compare.sh [size-MB] [threads]` runs gzip-ng against the system gzip and pigz where installed, through hyperfine when available and a built-in best-of-3 timer otherwise.

## Project notes

- Plan and progress: https://gist.github.com/nmoinvaz/4f88555bdf30d0d0850f062525a12738
- Engine experiment and benchmarks: https://gist.github.com/nmoinvaz/fbcb47d4c1b903c709953d8c7fae3cfc
