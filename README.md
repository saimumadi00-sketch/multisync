# MultiSync - Multi-Threaded File Compression Tool

> Operating Systems Project - 300-Level | Spring 2026

MultiSync compresses and decompresses files concurrently using POSIX threads
and Run-Length Encoding (RLE). Version 3 replaces the earlier one-thread-per-
file batching model with a bounded thread pool, adds CRC32 integrity checking,
supports output directories, handles SIGINT gracefully, and can print per-file
compression statistics.

---

## Project Structure

```
multisync/
├── include/
│   ├── logger.h       # Thread-safe logger API
│   ├── rle.h          # RLE compression API and header constants
│   ├── threadpool.h   # Bounded work queue and worker pool API
│   └── worker.h       # Job descriptor and worker entry point
├── src/
│   ├── logger.c       # Mutex-protected printf wrapper
│   ├── main.c         # CLI, argument parsing, dispatch, summary reporting
│   ├── rle.c          # RLE codec plus CRC32 implementation
│   ├── threadpool.c   # pthread pool with mutex/condition-variable queue
│   └── worker.c       # Reads, processes, writes, and records job stats
├── tests/
│   └── run_tests.sh   # Automated test suite (T1-T22, 26 checks)
├── samples/
│   ├── repetitive.txt
│   ├── natural.txt
│   └── alternating.txt
├── docs/
│   └── report.md
├── .gitignore
├── Makefile
└── README.md
```

---

## Build

**Requirements:** GCC (C11), GNU Make, Linux, pthreads

```bash
make          # build ./multisync
make clean    # remove binaries and generated test outputs
make test     # build and run all automated checks
make valgrind # run under Valgrind memcheck and helgrind
```

The project is compiled with:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -g -Iinclude -D_POSIX_C_SOURCE=200809L
```

---

## Usage

```bash
# Compress one file
./multisync file.txt
# produces file.txt.rle

# Compress multiple files using the default pool size min(files, 4)
./multisync file1.txt file2.txt file3.bin

# Limit the worker pool to 2 threads
./multisync -j 2 file1.txt file2.txt file3.txt file4.txt

# Write all outputs to a directory
./multisync -o compressed file1.txt file2.txt

# Decompress
./multisync -d file.txt.rle
# produces file.txt.out

# Decompress into a directory and print stats
./multisync -d -s -o restored file.txt.rle

# Show help
./multisync -h
```

### Flags

| Flag | Description |
|------|-------------|
| `-d` | Decompress mode. Default is compression. |
| `-j N` | Worker pool size limit. Default is `min(number_of_files, 4)`. |
| `-o DIR` | Write every output file into `DIR`. The directory is created with `mkdir()` and `S_IRWXU` if missing. |
| `-s` | Print a statistics table after all jobs complete. |
| `-h` | Print usage. |

All flags must come before file arguments.

---

## Output Filenames

Without `-o`, outputs are written beside the input path:

| Mode | Input | Output |
|------|-------|--------|
| Compress | `dir/file.txt` | `dir/file.txt.rle` |
| Decompress | `dir/file.txt.rle` | `dir/file.txt.out` |
| Decompress | `dir/other.bin` | `dir/other.bin.out` |

With `-o DIR`, outputs are written inside `DIR` using the input basename:

| Mode | Input | Output |
|------|-------|--------|
| Compress | `data/file.txt` | `DIR/file.txt.rle` |
| Decompress | `data/file.txt.rle` | `DIR/file.txt.out` |

If the `-o` path exists but is not a directory, MultiSync exits with a clear
error. Parent directories are not created recursively.

---

## Statistics Report

Passing `-s` prints a table after every submitted job finishes:

```text
File | Original | Compressed | Ratio | Time(ms) | Status
---- | -------- | ---------- | ----- | -------- | ------
input.txt | 1024 | 40 | 3.91% | 0.42 | OK
total bytes saved | 984
average ratio | 3.91%
```

For compression, `Original` is the input size and `Compressed` is the `.rle`
output size. For decompression, the same columns are reported in logical file
format terms: `Original` is the restored output size and `Compressed` is the
compressed input size. Failed and cancelled jobs remain in the table with
`FAIL` or `CANCELLED` status.

---

## Thread Pool Design

Version 3 uses a fixed-size pool instead of creating and joining one pthread
per file batch. `main.c` creates a `threadpool_t`, then submits `job_t` pointers
into a bounded FIFO queue. The queue is protected by one mutex and two condition
variables:

| Condition variable | Purpose |
|--------------------|---------|
| `not_empty` | Worker threads sleep here until a job is available. |
| `not_full` | The main dispatch loop sleeps here when the bounded queue is full. |

Each pool thread repeatedly dequeues one job and calls `worker_run(job)`.
`threadpool_shutdown()` marks the pool as closing, wakes sleepers, drains any
already queued jobs, joins all pool threads, and destroys the synchronization
objects.

---

## Signal Handling

MultiSync installs a `SIGINT` handler with `sigaction()`. The handler only sets
a `volatile sig_atomic_t` flag, which is safe inside a signal handler. The main
dispatch loop and timed queue submit path check that flag and stop submitting
new jobs after Ctrl+C. Jobs that are already running or already queued are
allowed to finish.

On interruption, MultiSync prints:

```text
Interrupted — N jobs cancelled
```

and exits non-zero.

---

## RLE Format

### Version 3 wire format

Every `.rle` file starts with a 16-byte header:

```text
[4 bytes] Magic:         0x52 0x4C 0x45 0x00  ("RLE\0")
[8 bytes] Original size: uint64_t, little-endian
[4 bytes] CRC32:         CRC32 of original uncompressed data, little-endian
[N bytes] RLE pairs:     (count, value) pairs
```

Payload encoding is unchanged:

```text
[count (1 byte)] [value (1 byte)]
```

Example payload: `AAABBC` becomes `03 41 02 42 01 43`, plus the 16-byte header.

During decompression, MultiSync validates the magic bytes, decodes exactly the
stored original size, recomputes CRC32 over the decompressed output, and fails
the job if the checksum does not match. CRC32 uses the standard reflected
polynomial `0xEDB88320`.

### Breaking compatibility note

Version 3 is a breaking format change. Old version 2 `.rle` files used a
12-byte header without CRC32. They will fail decompression under v3 because the
decoder expects a 16-byte header and verifies the CRC. Re-compress source files
with this version to produce compatible archives.

---

## Sample Data

The `samples/` directory contains small text files for manual demos:

| File | Purpose |
|------|---------|
| `samples/repetitive.txt` | Shows where RLE compresses well. |
| `samples/natural.txt` | Shows normal text that may expand. |
| `samples/alternating.txt` | Shows worst-case alternating patterns. |

Generated stress-test data is created automatically by `make test` under
`tests/data/`, including random binary data, long runs over 255 bytes,
single-byte files, alternating text, CRC-corrupted archives, SIGINT fixtures,
and invalid output-directory cases.

---

## Known Limitations

- RLE is ineffective on random or already-compressed data and may expand it.
- Max 64 input files per invocation.
- `-o` creates only the final directory, not missing parent directories.
- Empty input files currently fail gracefully instead of producing an empty
  archive.

---

## OS Concepts Demonstrated

| Concept | Where |
|---------|-------|
| Thread pool | `pthread_create` / `pthread_join` in `src/threadpool.c` |
| Bounded producer-consumer queue | `pthread_mutex_t`, `not_full`, `not_empty` in `src/threadpool.c` |
| Mutex synchronization | Logger mutex in `src/logger.c` |
| Signal handling | `sigaction()` and `volatile sig_atomic_t` in `src/main.c` |
| File I/O | `fopen`, `fread`, `fwrite`, `fclose` in `src/worker.c` |
| Dynamic memory | Per-file buffers and thread-pool arrays |
| Wall-clock timing | `clock_gettime(CLOCK_MONOTONIC)` |
| Integrity checking | CRC32 in `src/rle.c` |
| Error handling | Checked system/library calls with graceful job failure |
