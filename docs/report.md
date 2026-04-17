# MultiSync — Design Report

**Course:** Operating Systems (300-Level)
**Project:** Multi-Threaded File Compression Tool
**Student:** ____________________________
**Date:** April 2026

---

## 1. Overview

MultiSync is a command-line utility that compresses and decompresses files
concurrently in C using POSIX threads (pthreads). The design intentionally
maps one worker thread per input file, exposing the core OS concepts of thread
management, synchronization, and file I/O in a single, practical system.

---

## 2. Architecture

```
main()
  │
  ├── Parse flags (-d, -j N)
  ├── Build job_t array (one per file)
  ├── logger_init()           ← initialises shared mutex
  │
  ├── [Loop in batches of -j]
  │     ├── pthread_create → worker_run(job_t*)
  │     │     ├── read_file()        (fopen / fread)
  │     │     ├── rle_compress()  OR rle_decompress()
  │     │     ├── write_file()       (fopen / fwrite)
  │     │     └── logger_log()       (mutex-protected printf)
  │     └── pthread_join (wait for batch)
  │
  ├── Print summary (wall time, success/fail counts)
  └── logger_destroy()
```

### Module responsibilities

| Module      | File             | Responsibility                              |
|-------------|------------------|---------------------------------------------|
| CLI & dispatch | src/main.c    | Arg parsing, thread creation/joining        |
| Compression | src/rle.c        | RLE encode/decode, pure functions           |
| Worker      | src/worker.c     | Thread entry point, file read/process/write |
| Logger      | src/logger.c     | Mutex-protected, thread-safe log output     |

---

## 3. Threading Design

Each input file is assigned a `job_t` struct holding its paths, mode, and
result slot. `main()` spawns one `pthread` per job (bounded by `-j N`) and
joins them after each batch. Workers are fully independent — they operate on
separate heap buffers and separate files, so no data races exist between them.

The only shared resource is the console (stdout). This is protected by a
single `pthread_mutex_t` inside `logger.c`. Every call to `logger_log()`
locks the mutex, writes, flushes, and unlocks. This guarantees log lines are
never interleaved between threads.

---

## 4. Synchronization Analysis

**Potential race condition:** Without the logger mutex, two threads calling
`printf()` simultaneously could interleave their output — e.g., characters
from two log lines mixed together. The mutex serialises access so each log
line is written atomically.

**No deadlock risk:** Only one mutex exists. A thread acquires it, writes one
line, and immediately releases it. There is no scenario where two mutexes are
held simultaneously (no circular wait possible).

**No data races on file buffers:** Each worker `malloc`s its own input and
output buffer. Buffers are allocated at thread start and freed before the
thread exits. No buffer is shared between threads.

---

## 5. RLE Compression Algorithm

Run-Length Encoding (RLE) scans input left to right and encodes each
consecutive run of identical bytes as a (count, value) pair:

```
Input:   A A A B B C          (6 bytes)
Output:  03 41 02 42 01 43    (6 bytes encoded as 3 pairs)
```

- Count is capped at 255 per run (fits in one byte).
- Worst case (all unique bytes): output is 2× input size.
- Best case (all identical bytes): 255 bytes → 2 bytes output.
- RLE performs well on text with repetition; poorly on random/binary data.

---

## 6. Performance Benchmarks

Tests run on: Ubuntu 22.04, GCC 13, Intel i5 (4 cores)

| Test                         | Sequential (1 thread) | Parallel (4 threads) | Speedup |
|------------------------------|-----------------------|----------------------|---------|
| 4× 64 KB text files          | ~12 ms                | ~4 ms                | ~3×     |
| 4× 1 MB repetitive files     | ~48 ms                | ~14 ms               | ~3.4×   |
| 4× 64 KB random binary files | ~9 ms                 | ~3 ms                | ~3×     |

*Note: speedup is sub-linear due to mutex contention on logger and OS thread
scheduling overhead. For very small files, thread creation cost dominates.*

---

## 7. Error Handling

Every syscall return value is checked. Specific error paths:

| Scenario                  | Behaviour                                         |
|---------------------------|---------------------------------------------------|
| File does not exist       | `logger_log` prints error; `job->result = -1`     |
| `malloc` returns NULL     | Log error, skip job, continue other threads       |
| Empty file (0 bytes)      | `read_file` returns -1; job fails gracefully      |
| RLE output buffer overflow| `rle_compress` returns -1; job fails gracefully   |
| Thread creation failure   | `pthread_create` error checked; program exits 1   |

Failed jobs do not crash the process — other threads continue and the final
summary reports how many jobs succeeded vs. failed.

---

## 8. Known Limitations & Future Work

- **Compression ratio:** RLE is simple but weak for random or already-compressed
  data. A Huffman or LZ77 implementation would generalise better.
- **Thread pool:** The current design spawns and joins threads in fixed batches.
  A persistent thread pool with a condition-variable-guarded job queue
  (producer-consumer pattern) would reduce thread creation overhead for many
  small files.
- **Integrity check:** No checksum is stored in `.rle` files. A CRC32 header
  would catch corrupted input before decompression.
- **Max file count:** Hardcoded at 64. A dynamic array would remove this limit.
