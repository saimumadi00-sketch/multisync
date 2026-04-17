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
│   ├── rle.c          # RLE compress / decompress implementation
│   ├── worker.c       # Thread worker: reads, processes, writes file
│   └── logger.c       # Mutex-protected printf wrapper
├── tests/
│   └── run_tests.sh   # Automated test suite (7 test cases)
├── docs/
│   └── report.md      # Design report (submit alongside code)
├── Makefile
└── README.md
```

---

## Build

**Requirements:** GCC (C11), GNU Make, Linux, pthreads (standard on Linux)

```bash
make          # build → ./multisync
make clean    # remove binaries and test output files
make test     # build + run all 7 test cases
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

---

## RLE Format

Compression encodes each run of identical bytes as a 2-byte pair:

```
[count (1 byte)] [value (1 byte)]
```

Example: `AAABBC` → `03 41 02 42 01 43`

- Max run length per pair: 255
- Worst case output size: 2× input (every byte unique)
- Best case: highly repetitive data (e.g. 255 identical bytes → 2 bytes)

---

## Output Filenames

| Mode        | Input          | Output           |
|-------------|----------------|------------------|
| Compress    | `file.txt`     | `file.txt.rle`   |
| Decompress  | `file.txt.rle` | `file.txt.out`   |
| Decompress  | `other.bin`    | `other.bin.out`  |

---

## Known Limitations

- RLE is ineffective on random/already-compressed data (output may be larger)
- Max 64 input files per invocation
- Decompressed output buffer is bounded at 128× input size
- No checksum or integrity verification of `.rle` files

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
