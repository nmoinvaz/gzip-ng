# gzip-ng

A gzip replacement built on [zlib-ng](https://github.com/zlib-ng/zlib-ng), with parallel compression into independent deflate blocks and parallel decompression of block-structured files, including pigz -i and pigz --rsyncable -i output.

Early skeleton. The engine and CLI are being ported from the zlib-ng prototype branch.

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

## Project notes

- Plan and progress: https://gist.github.com/nmoinvaz/4f88555bdf30d0d0850f062525a12738
- Engine experiment and benchmarks: https://gist.github.com/nmoinvaz/fbcb47d4c1b903c709953d8c7fae3cfc
