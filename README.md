Random access decompression, using zstd.

# General

Requires zstd
[built](https://github.com/facebook/zstd/tree/v1.4.9/lib#multithreading-support)
with multithreading support.

# Build

```sh
make
```

# Test

Elementary test to compress, decomporess and validate a user-specified file. See
`test/test.c`.

```sh
cd test
make test   # builds zstd from source and links against it
./test <path-to-uncompressed-file>
```

# Benchmark

Elementary **compression-only** benchmark on a user-specified file. Allows
controlling various parameters and measures time and resource usage. Tries to
eliminate I/O variability by **loading the whole file to memory** at startup.
See `test/benchmark.c`.

```sh
cd test
make benchmark  # builds zstd from source and links against it
```

For a single run:

```sh
./benchmark <path-to-uncompressed-file>
```

For multiple runs:

```sh
# See/edit benchmark.sh for the # of workers/min frame sizes to test
./benchmark.sh <path-to-uncompressed-file> | tee report.txt
./report.awk
```

# TODO

- More tests: standalone, multi-threaded.
- Pluggable I/O.
- Pluggable memory management.
- Parameterize/tune cache size and compression level.
- Dictionaries?
- OPT: Stop relying on zstd's multi-threading?
  - Pro: could use vanilla-built zstd.
  - Pro: total control over the threading.
  - Con: no intra-frame parallelism.
