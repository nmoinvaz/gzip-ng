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

## Usage

```
gzip-ng [-c] [-d] [-k] [-f|-h|-R|-F|-T] [-A] [-b size] [-p threads] [-0 to -9] [files...]
```

Compresses files in place, file to file.gz, removing the input unless -k. With no files it filters stdin to stdout. Installed as gunzip, zcat, or gzcat it presets -d and -c the way gzip's aliases do.

- -c : write to standard output, keep the files
- -d : decompress, parallel automatically for block or pigz -i style input
- -k : keep input files
- -f, -h, -R, -F : deflate strategies, filtered, huffman only, run length, fixed
- -T : store without compressing
- -A : text mode, accepted for compatibility
- -b size : compress in independent blocks of size, K, M, and G suffixes
- -p threads : threads to use, 0 picks the number of CPUs
- -0 to -9 : compression level, 6 by default

## Project notes

- Plan and progress: https://gist.github.com/nmoinvaz/4f88555bdf30d0d0850f062525a12738
- Engine experiment and benchmarks: https://gist.github.com/nmoinvaz/fbcb47d4c1b903c709953d8c7fae3cfc
