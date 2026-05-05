# MultiSync - Multi-Threaded File Compression Tool

> Operating Systems Project - 300-Level | Spring 2026

MultiSync compresses and decompresses files using POSIX threads and
Run-Length Encoding (RLE). The current version uses a bounded thread pool,
CRC32 integrity checks, adaptive stored mode, optional progress bars,
stdin/stdout streaming, compression levels, benchmark mode, output directories,
statistics reporting, and graceful SIGINT shutdown.

---

## Project Structure

```
multisync/
├── include/
│   ├── logger.h       # Thread-safe logger API
│   ├── progress.h     # Shared progress slots and monitor API
│   ├── rle.h          # RLE API and unified 17-byte header constants
│   ├── threadpool.h   # Bounded work queue and worker pool API
│   └── worker.h       # Job descriptor, worker entry point, benchmark API
├── src/
│   ├── logger.c       # Mutex-protected printf wrapper
│   ├── main.c         # CLI, dispatch, progress, stats, benchmark reporting
│   ├── progress.c     # Progress monitor thread and bar rendering
│   ├── rle.c          # RLE codec, CRC32, entropy, levels, stored mode
│   ├── threadpool.c   # pthread pool with mutex/condition-variable queue
│   └── worker.c       # Reads, processes, writes, progress, benchmark runs
├── tests/
│   └── run_tests.sh   # Automated test suite (T1-T32, 36 checks)
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

**Requirements:** GCC (C11), GNU Make, Linux, pthreads, libm

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

The final link includes:

```bash
-lpthread -lm
```

---

## Usage

```bash
# Compress one file
./multisync file.txt

# Compress multiple files with the default pool size min(files, 4)
./multisync file1.txt file2.txt file3.bin

# Limit the worker pool to 2 threads
./multisync -j 2 file1.txt file2.txt file3.txt file4.txt

# Adaptive mode: store high-entropy inputs instead of expanding them
./multisync -a random.bin

# Maximum compression level with delta preprocessing
./multisync -l 3 samples/alternating.txt

# Show progress bars
./multisync -p -j 2 big1.bin big2.bin

# Write all outputs to a directory
./multisync -o compressed file1.txt file2.txt

# Decompress; level is read from the header
./multisync -d file.txt.rle

# Pipe mode
cat file.txt | ./multisync - > file.txt.rle
cat file.txt.rle | ./multisync -d - > file.txt.out

# Benchmark compression without writing output archives
./multisync --bench file1.txt file2.bin

# Print stats after normal jobs finish
./multisync -s file1.txt file2.txt
```

### Flags

| Flag | Description |
|------|-------------|
| `-d` | Decompress mode. Default is compression. |
| `-j N` | Worker pool size limit. Default is `min(number_of_files, 4)`. |
| `-o DIR` | Write every output file into `DIR`; creates the directory with `mkdir()` and `S_IRWXU` if missing. |
| `-s` | Print a statistics table after all jobs complete. |
| `-a` | Auto mode. Samples the first 4096 bytes and stores high-entropy input raw when entropy is greater than 7.2. |
| `-p` | Print real-time progress bars from a dedicated monitor thread. |
| `-l 1` | Fast standard RLE. |
| `-l 2` | Default RLE behavior with logical long-run handling across 255-byte pair boundaries. |
| `-l 3` | Level 2 plus delta encoding preprocessing; decompression reverses it automatically from the stored level. |
| `--bench` | Run five compression trials per file, print timing/throughput, and write no output archives. |
| `-h` | Print usage. |

All flags must come before file arguments. Pipe mode with `-` is intentionally
single-job only and cannot be combined with `-o`, `-s`, `-p`, or `--bench`,
because stdout must remain clean binary data.

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

With input `-`, MultiSync reads stdin and writes stdout. Logger and normal
summary output are suppressed in that mode to avoid corrupting binary output.

---

## RLE Format

### Unified 17-byte header

Every archive now starts with the same 17-byte header:

```text
Byte(s)   Field                Description
0..3      magic                0x52 0x4C 0x45 0x00 ("RLE\0")
4..11     original_size        uint64_t, little-endian
12..15    crc32                CRC32 of original uncompressed data
16        compression_flags    bit 0: 1 = STORED, 0 = RLE_COMPRESSED
                               bits 1-2: compression level 1..3
17..N     payload              raw bytes for STORED, RLE pairs otherwise
```

`RLE_MODE_STORED` means the payload is the original bytes. This is selected by
`-a` when Shannon entropy over the first 4096 bytes is greater than 7.2.

`RLE_MODE_RLE` means the payload is `(count, value)` pairs:

```text
[count (1 byte)] [value (1 byte)]
```

The CRC32 uses the standard reflected polynomial `0xEDB88320`. Decompression
validates magic, size, compression flags, decoded length, and CRC. The level is
always read from byte 16, so users do not pass `-l` when decompressing.

### Compatibility Note

This is a breaking format change from the previous 16-byte header. Older `.rle`
files without the compression-flags byte will fail decompression. Re-compress
source files with this version to produce compatible archives.

---

## Adaptive Mode

`-a` estimates entropy using:

```text
H = -sum(p_i * log2(p_i))
```

for all byte values 0-255 over the first 4096 bytes, or the whole file if it is
smaller. If `H > 7.2`, MultiSync writes a STORED archive instead of RLE. This
keeps random or already-compressed data from expanding to roughly twice its
original size.

---

## Progress Bars

`-p` creates one monitor thread in `main.c`, separate from the worker pool.
Workers update shared `progress_t` slots with processed byte counts. The monitor
wakes every 100 ms with `nanosleep()`, reads slots under a `pthread_rwlock_t`,
and redraws active jobs on stderr:

```text
[job-00] ████████░░░░░░░░ 52% (51200/98304 bytes)
```

Progress output is disabled for pipe mode.

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

For compression, `Original` is the input size and `Compressed` is the archive
size. For decompression, `Original` is the restored output size and
`Compressed` is the compressed input size. Failed and cancelled jobs remain in
the table.

---

## Benchmark Mode

`--bench` runs each input through compression five times in the current process,
single-threaded per file, and writes no `.rle` output. It reports:

```text
File | Size | Min(µs) | Max(µs) | Avg(µs) | MB/s
```

This mode is for profiling the RLE algorithm and does not affect normal
compress/decompress behavior.

---

## Thread Pool Design

Normal file jobs use a fixed-size pool. `main.c` submits `job_t` pointers into
a bounded FIFO queue protected by one mutex and two condition variables:

| Condition variable | Purpose |
|--------------------|---------|
| `not_empty` | Worker threads sleep here until a job is available. |
| `not_full` | The main dispatch loop sleeps here when the bounded queue is full. |

Each pool thread dequeues one job and calls `worker_run(job)`.
`threadpool_shutdown()` marks the pool as closing, wakes sleepers, drains
already queued jobs, joins all pool threads, and destroys synchronization
objects.

---

## Signal Handling

MultiSync installs a `SIGINT` handler with `sigaction()`. The handler only sets
a `volatile sig_atomic_t` flag. The dispatch loop and timed queue submit path
check that flag and stop submitting new jobs after Ctrl+C. Running and already
queued jobs finish.

On interruption, MultiSync prints:

```text
Interrupted — N jobs cancelled
```

and exits non-zero.

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
pipe-mode fixtures, progress logs, and benchmark checks.

---

## Known Limitations

- RLE is still simple and may not beat general-purpose compressors.
- Max 64 input files per invocation.
- `-o` creates only the final directory, not missing parent directories.
- Empty input files currently fail gracefully instead of producing an empty
  archive.
- Pipe mode supports exactly one `-` job per invocation.

---

## OS Concepts Demonstrated

| Concept | Where |
|---------|-------|
| Thread pool | `pthread_create` / `pthread_join` in `src/threadpool.c` |
| Bounded producer-consumer queue | `pthread_mutex_t`, `not_full`, `not_empty` in `src/threadpool.c` |
| Reader-writer synchronization | `pthread_rwlock_t` in `src/progress.c` |
| Mutex synchronization | Logger mutex in `src/logger.c` |
| Signal handling | `sigaction()` and `volatile sig_atomic_t` in `src/main.c` |
| Pipe I/O | stdin/stdout handling in `src/worker.c` |
| File I/O | `fopen`, `fread`, `fwrite`, `fclose` in `src/worker.c` |
| Dynamic memory | File buffers, stdin growth buffer, pool arrays, progress slots |
| Wall-clock timing | `clock_gettime(CLOCK_MONOTONIC)` |
| Entropy math | Shannon entropy with `log2()` in `src/rle.c` |
| Integrity checking | CRC32 in `src/rle.c` |
| Error handling | Checked system/library calls with graceful job failure |
