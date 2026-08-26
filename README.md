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
gzip-ng [options] [files...]
```

Compresses files in place, file to file.gz, removing the input unless kept. With no files, or -, it filters stdin to stdout. Options and files mix in any order, short options cluster, and installed as gunzip, zcat, or gzcat it presets -d and -c the way gzip's aliases do.

- -c --stdout : write to standard output, keep the files
- -d --decompress : decompress, parallel automatically for block or pigz -i style input
- -f --force : overwrite outputs, compress to a terminal
- -k --keep : keep input files
- -h --help, --version
- -H --huffman, -U --rle, --filtered, --fixed : deflate strategies
- -T : store without compressing
- -A : text mode, accepted for compatibility
- -b --blocksize size : compress in independent blocks, K, M, and G suffixes
- -p --processes n : threads to use, 0 picks the number of CPUs
- -1 --fast .. -9 --best : compression level, 6 by default
- -l --list : list compressed file contents, -v adds method, crc, and date
- -t --test : check integrity without writing
- -n --no-name / -N --name : stored name and time, saved by default, restored only with -N
- -r --recursive : descend into directories
- -v --verbose / -q --quiet : per-file reports, or no warnings
- -L --license
- --rsyncable : content-defined block ends, edits stay local for rsync
- --synchronous : fsync outputs before removing inputs

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
