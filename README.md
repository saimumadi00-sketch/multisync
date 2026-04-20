# MultiSync — Multi-Threaded File Compression Tool

> Operating Systems Project — 300-Level | Spring 2026

MultiSync compresses and decompresses files concurrently using POSIX threads.
Each file gets its own worker thread. A mutex-protected logger ensures clean,
race-free output. Compression uses Run-Length Encoding (RLE).

---

## Project Structure

```
multisync/
├── include/
│   ├── rle.h          # RLE compression API
│   ├── worker.h       # Thread job descriptor & entry point
│   └── logger.h       # Thread-safe logger API
├── src/
│   ├── main.c         # CLI, argument parsing, thread dispatch
│   ├── rle.c          # RLE compress / decompress with header
│   ├── worker.c       # Thread worker: reads, processes, writes file
│   └── logger.c       # Mutex-protected printf wrapper
├── tests/
│   └── run_tests.sh   # Automated test suite (8 test cases)
├── docs/
│   └── report.md      # Design report (submit alongside code)
├── .gitignore
├── Makefile
└── README.md
```

---

## Build

**Requirements:** GCC (C11), GNU Make, Linux, pthreads (standard on Linux)

```bash
make          # build → ./multisync
make clean    # remove binaries and test output files
make test     # build + run all 8 test cases
make valgrind # run under Valgrind memcheck + helgrind
```

---

## Usage

```bash
# Compress one file
./multisync file.txt
# → produces file.txt.rle

# Compress multiple files in parallel
./multisync file1.txt file2.txt file3.bin

# Limit parallelism to 2 threads at a time
./multisync -j 2 file1.txt file2.txt file3.txt file4.txt

# Decompress
./multisync -d file.txt.rle
# → produces file.txt.out

# Show help
./multisync -h
```

### Flags

| Flag    | Description                                      |
|---------|--------------------------------------------------|
| `-d`    | Decompress mode (default: compress)              |
| `-j N`  | Max concurrent threads (default: one per file)   |
| `-h`    | Print usage                                      |

**Important:** All flags must come *before* file arguments.

---

## RLE Format

### Wire format
Each `.rle` file contains:
```
[4 bytes] Magic:        0x52 0x4C 0x45 0x00  ("RLE\0")
[8 bytes] Original size: uint64_t, little-endian
[N bytes] RLE pairs:    (count, value) pairs
```

### Compression algorithm
For each run of identical bytes:
```
[count (1 byte)] [value (1 byte)]
```

Example: `AAABBC` → `03 41 02 42 01 43` (plus 12-byte header)

**Characteristics:**
- Max run length per pair: 255
- Worst case output: original + header (every byte unique)
- Best case: original size / 127 (255 identical bytes → 2 bytes)
- RLE performs well on text with repetition; poorly on random/binary data

---

## Output Filenames

| Mode        | Input          | Output           |
|-------------|----------------|------------------|
| Compress    | `file.txt`     | `file.txt.rle`   |
| Decompress  | `file.txt.rle` | `file.txt.out`   |
| Decompress  | `other.bin`    | `other.bin.out`  |

---

## Recent Fixes (v2)

This version includes critical bug fixes:

- **pthread_create return value now checked** — exits cleanly on thread creation failure
- **RLE header added** — stores original size for exact decompression buffer allocation
- **Negative compression ratio display fixed** — now shows "171.4% larger" not "-85.7% smaller"
- **Flag parsing improved** — flags must come first; warning if options detected after files
- **Logger optimized** — mutex held for microseconds not milliseconds
- **va_list handling** — va_end called before mutex (eliminates theoretical UB)

**⚠️ Breaking change:** Old `.rle` files from v1 will not decompress. Re-compress with this version.

---

## Known Limitations

- RLE is ineffective on random/already-compressed data (output may be larger)
- Max 64 input files per invocation
- No integrity check beyond magic validation

---

## OS Concepts Demonstrated

| Concept              | Where                                        |
|----------------------|----------------------------------------------|
| Threads              | `pthread_create` / `pthread_join` in main.c  |
| Mutex synchronization| `pthread_mutex_lock/unlock` in logger.c      |
| File I/O             | `fopen/fread/fwrite/fclose` in worker.c      |
| Dynamic memory       | `malloc/free` for per-thread file buffers    |
| Wall-clock timing    | `clock_gettime(CLOCK_MONOTONIC)` in main.c   |
| Error handling       | All syscalls checked; graceful failure paths |
