# MultiSync — Design Report

Design report for a pthread-based multi-file compression tool.

---

## 1. Overview

MultiSync is a command-line utility that compresses and decompresses files
concurrently in C using POSIX threads (pthreads). The design intentionally maps
one worker thread per input file, exposing the core OS concepts of thread
management, synchronization, and file I/O in a single, practical system.

This v2 release includes critical bug fixes and an improved RLE format with a
file header for exact decompression buffer allocation.

---

## 2. Architecture

```
main()
  │
  ├── Parse flags (-d, -j N)
  ├── Validate flag order (must come before files)
  ├── Build job_t array (one per file)
  ├── logger_init()           ← initialises shared mutex
  │
  ├── [Loop in batches of -j]
  │     ├── pthread_create → worker_run(job_t*)
  │     │     ├── read_file()        (fopen / fread)
  │     │     ├── Compress:  rle_compress() + 12-byte header
  │     │     │  Decompress: read header, rle_decompress()
  │     │     ├── write_file()       (fopen / fwrite)
  │     │     └── logger_log()       (mutex-protected printf)
  │     └── pthread_join (wait for batch, check rc)
  │
  ├── Print summary (wall time, success/fail counts)
  └── logger_destroy()
```

### Module responsibilities

| Module      | File             | Responsibility                              |
|-------------|------------------|---------------------------------------------|
| CLI & dispatch | src/main.c    | Arg parsing, thread create/join, error check|
| Compression | src/rle.c        | RLE encode/decode, header format            |
| Worker      | src/worker.c     | Thread entry point, file read/process/write |
| Logger      | src/logger.c     | Mutex-protected, thread-safe log output     |

---

## 3. RLE File Format (v2)

Each `.rle` file begins with a 12-byte header:

```
Bytes 0-3:   Magic         0x52 0x4C 0x45 0x00  ("RLE\0")
Bytes 4-11:  Original size uint64_t, little-endian
Bytes 12+:   RLE payload   (count, value) pairs
```

**Advantages:**
- Decompression knows exact buffer size → no overflow risk, no guessing
- Magic validation catches corrupt/wrong files immediately
- Endianness explicit (little-endian for portability)

**Example:**
```
Input:     AAABBC           (6 bytes)
RLE pairs: 03 41 02 42 01 43 (6 bytes as 3 pairs)
Header:    RLE\0 06000000000000000000  (magic + size)
.rle file: [header 12 bytes] [payload 6 bytes]  → 18 bytes total
```

---

## 4. Threading Design

Each input file is assigned a `job_t` struct holding its paths, mode, and
result slot. `main()` spawns one `pthread` per job (bounded by `-j N`) and
joins them after each batch. **Threads are fully independent** — they operate
on separate heap buffers and separate files, so no data races exist between
them.

**The only shared resource is the console (stdout).** This is protected by a
single `pthread_mutex_t` inside `logger.c`. Every call to `logger_log()`:
1. Formats the message into a local buffer (no lock)
2. Calls `va_end()` (no lock)
3. Acquires the mutex
4. Writes, flushes
5. Releases the mutex

This guarantees log lines are never interleaved between threads, while keeping
the critical section tiny (microseconds, not the full format time).

---

## 5. Synchronization Analysis

**Potential race condition:** Without the logger mutex, two threads calling
`printf()` simultaneously could interleave their output — characters from two
log lines mixed together. The mutex serialises access so each line is atomic.

**No deadlock risk:** Only one mutex exists. A thread acquires it, writes one
line, and immediately releases it. There is no scenario where two mutexes are
held simultaneously (no circular wait possible).

**No data races on file buffers:** Each worker `malloc`s its own input and
output buffer. Buffers are allocated at thread start and freed before the
thread exits. No buffer is shared between threads.

**pthread_create error handling:** Return value is now checked; if thread
creation fails, the program reports the error and exits cleanly rather than
attempting to join a garbage thread (undefined behavior).

---

## 6. Critical Bug Fixes (v2)

### Bug 1: pthread_create return value unchecked
**Before:** If thread creation failed (e.g., resource exhaustion), the program
would call `pthread_join` on a garbage thread descriptor — undefined behavior.
**After:** Check return code, report error, exit cleanly.
**Impact:** High (silent failure / crash in edge cases)

### Bug 2: RLE format has no header
**Before:** Decompression allocated output as `in_size * 128` — a blind guess
that crashes on edge-case files (large .rle files, or tiny files that expand).
**After:** .rle format now has 12-byte header storing exact original size.
Decompression reads the header, allocates precisely, and validates magic bytes.
**Impact:** Critical (data loss / crash risk)

### Bug 3: Negative compression ratio displays as "smaller"
**Before:** When RLE expanded a file (e.g., `hello.txt` with no repetition),
log said `-85.7% smaller` (confusing).
**After:** Detect ratio < 0 and report `171.4% larger`.
**Impact:** Medium (user confusion)

### Bug 4: in_size used after free(in_buf)
**Before:** `in_size` is a `long` copy (safe), but read as use-after-free to
reviewers. Logger still logged `in_size` after the buffer was freed.
**After:** Explicitly save as `saved_in_size` before freeing.
**Impact:** Low (code smell, not a real bug)

### Bug 5: va_end called outside mutex
**Before:** `va_list` was consumed inside the lock, but `va_end` called after
`pthread_mutex_unlock`. Theoretical UB (very unlikely to trigger).
**After:** Format into local buffer first, call `va_end`, then lock+write.
**Bonus:** Mutex now held for microseconds not milliseconds.
**Impact:** Low (theoretical UB)

### Bug 6: Flag parsing broken
**Before:** `-d` or `-j` after filenames would silently skip them, giving a
confusing "no input files" error.
**After:** Flags must come first. Clean `while argv[i][0] == '-'` loop.
Added warning if option-like string detected after files.
**Impact:** Medium (user confusion)

---

## 7. Performance Benchmarks

Tests run on: Ubuntu 22.04, GCC 13, 4-core machine, 256 KB test files

| Test File Type         | Size       | Compressed Size | Ratio     |
|------------------------|------------|-----------------|-----------|
| Random binary          | 262 KB     | 522 KB          | 99.2% *larger* |
| Highly repetitive text | 262 KB     | 2.1 KB          | 99.2% smaller  |
| Natural text           | 5 bytes    | 22 bytes        | 340% larger    |

**Analysis:**
- **Random data:** RLE is useless; every byte is unique → header + 2 bytes per byte
- **Repetitive text:** RLE shines; 255 identical bytes → 2 bytes + header
- **Natural text:** Most characters are unique; file expands slightly

**Speedup (parallel vs sequential):** Sub-linear due to:
- Mutex contention on logger output
- OS thread scheduling overhead
- For very small files, thread creation cost dominates

Recommendation: Use `-j N` for workloads with many medium-to-large files;
for small files, sequential compression is faster.

---

## 8. Error Handling

Every syscall return value is checked. Specific error paths:

| Scenario                  | Behaviour                                         |
|---------------------------|---------------------------------------------------|
| File does not exist       | `logger_log` prints error; job fails              |
| `malloc` returns NULL     | Log error, skip job, continue other threads       |
| Empty file (0 bytes)      | `read_file` returns -1; job fails gracefully      |
| Corrupt `.rle` file       | Magic validation fails; clear error message       |
| RLE buffer overflow       | `rle_compress/decompress` returns -1; job fails   |
| Thread creation failure   | `pthread_create` error checked; program exits 1   |
| Flag after files          | Warning printed; files treated as arguments       |

Failed jobs do not crash the process — other threads continue and the final
summary reports how many jobs succeeded vs. failed.

---

## 9. Testing

**Automated tests (14 checks, all passing):**
1. Basic compress: command succeeds on `hello.txt`
2. Output file exists: verifies `.rle` creation
3. Roundtrip: compress then decompress matches original
4. Multi-file: parallel compression of 3 files
5. Large file: 1 KB file roundtrip
6. Parallel flag: `-j 2` works correctly
7. Empty file: fails gracefully (exit 1, no crash)
8. Bad path: fails gracefully
9. Single-byte file: smallest valid non-empty input roundtrips
10. Long run: 700 repeated bytes roundtrip correctly
11. Long run size: confirms split into 255-byte RLE chunks
12. Binary pattern: NUL bytes and `0xFF` bytes roundtrip correctly
13. Alternating data: worst-case expanding input still roundtrips
14. Corrupt `.rle`: invalid magic/header fails cleanly

**Test data coverage:**

| Data Type | Purpose |
|-----------|---------|
| Short text | Basic CLI smoke test |
| Repetitive text | Best-case RLE compression |
| Long repeated run | Verifies 255-byte count limit handling |
| Alternating text | Worst-case RLE expansion |
| Random binary | Large binary input stress case |
| Binary pattern | NUL and high-byte value safety |
| Empty file | Graceful failure path |
| Corrupt `.rle` | Header/magic validation |

**Valgrind memcheck:** 22 allocs, 22 frees, 0 leaks
**Valgrind helgrind:** 0 thread errors, 0 data races

---

## 10. Known Limitations & Future Work

- **Compression ratio:** RLE is simple but weak for random or already-compressed
  data. Huffman or LZ77 would generalise better.
- **Max 64 files:** Hardcoded limit. A dynamic array would remove this.
- **No CRC:** No checksum stored in `.rle` files. A CRC32 header would catch
  corruption before decompression.
- **Thread pool:** Current design spawns and joins in fixed batches. A
  persistent pool with a condition-variable-guarded job queue would reduce
  thread creation overhead for many small files.
- **Backward compatibility:** v1 `.rle` files will not decompress (breaking
  change). This is acceptable for a course-sized systems project.
