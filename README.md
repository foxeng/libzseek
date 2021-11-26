Random access decompression, using zstd or lz4.

# General

Requires zstd
[built](https://github.com/facebook/zstd/tree/v1.5.0/lib#multithreading-support)
with multithreading support for operations with >1 workers.

# Build

```sh
autoreconf --install  # requires autoconf-archive
./configure
make
```

# Test

Elementary test to compress, decomporess and validate a user-specified file. See
`test/example.c`.

```sh
./example --zstd|--lz4 <path-to-uncompressed-file>
```

# Benchmark

Elementary **compression-only** benchmark on a user-specified file. Allows
controlling various parameters and measures time and resource usage. Tries to
eliminate I/O variability by **loading the whole file to memory** at startup.
See `test/benchmark.c`.

For a single run:

```sh
./benchmark --zstd|--lz4 <path-to-uncompressed-file> <workers> <frame-size>
```

For multiple runs:

```sh
# See/edit benchmark.sh for the # of workers/min frame sizes to test
./benchmark.sh --zstd|--lz4 <path-to-uncompressed-file> | tee report.txt
./report.awk
```

# TODO

- More tests: standalone, multi-threaded.
- Pluggable memory management.
- Dictionaries?
- OPT: Stop relying on zstd's multi-threading?
  - Pro: could use vanilla-built zstd.
  - Pro: total control over the threading.
  - Con: no intra-frame parallelism.
