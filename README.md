# MultiSync

MultiSync is a C11 command-line file compression tool for Linux. It compresses
and decompresses files with Run-Length Encoding (RLE), runs multiple jobs through
a pthread-based thread pool, verifies data with CRC32, and includes practical
terminal features such as progress bars, benchmark mode, statistics, and
stdin/stdout pipeline support.

## Feature Overview

| Feature | What it does |
|---------|--------------|
| Multi-threaded compression | Uses a fixed-size worker pool with a bounded queue. |
| RLE compression | Stores repeated byte runs as `(count, value)` pairs. |
| CRC32 integrity check | Detects corrupted compressed files during decompression. |
| Adaptive mode | Uses entropy sampling to store random/high-entropy files instead of expanding them. |
| Compression levels | Supports levels 1, 2, and 3, with level 3 using delta preprocessing. |
| Progress bars | Shows real-time per-job progress from a monitor thread. |
| Statistics report | Prints size, ratio, timing, and status per file. |
| Output directory mode | Writes all outputs to a selected directory. |
| Pipe mode | Reads stdin and writes stdout with filename `-`. |
| Benchmark mode | Runs compression five times per file and reports min/max/avg speed. |
| Graceful SIGINT | Stops dispatching new jobs after Ctrl+C while allowing running jobs to finish. |

## Demo Video

<video src="docs/assets/multisync-demo.mp4" controls width="100%" title="MultiSync demo"></video>

If the video preview is not shown by your Markdown viewer, open
[docs/assets/multisync-demo.mp4](docs/assets/multisync-demo.mp4).

## Project Layout

```text
multisync/
├── include/
│   ├── logger.h
│   ├── progress.h
│   ├── rle.h
│   ├── threadpool.h
│   └── worker.h
├── src/
│   ├── logger.c
│   ├── main.c
│   ├── progress.c
│   ├── rle.c
│   ├── threadpool.c
│   └── worker.c
├── samples/
│   ├── alternating.txt
│   ├── natural.txt
│   └── repetitive.txt
├── tests/
│   └── run_tests.sh
├── docs/
│   └── report.md
├── Makefile
└── README.md
```

## Requirements

- Linux or a Linux VM
- GCC with C11 support
- GNU Make
- pthreads
- math library `libm`

The Makefile builds with:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -g -Iinclude -D_POSIX_C_SOURCE=200809L
```

and links with:

```bash
-lpthread -lm
```

## Quick Start

Run these commands from the repository root:

```bash
make clean
make
```

Compress one sample file:

```bash
./multisync samples/repetitive.txt
```

Decompress it:

```bash
./multisync -d samples/repetitive.txt.rle
```

Verify the decompressed output matches the original:

```bash
cmp samples/repetitive.txt samples/repetitive.txt.out && echo "PERFECT MATCH"
```

Run the full test suite:

```bash
make test
```

Expected result:

```text
Results: 36 passed, 0 failed
```

## Demo Script

This short sequence is good for a project demo video.

### 1. Build

```bash
make clean
make
```

One-liner: MultiSync is compiled as a C11 pthread-based terminal program.

### 2. Show sample files

```bash
ls -lh samples/
```

One-liner: These are the small sample files used for the demo.

### 3. Compress a file

```bash
./multisync samples/repetitive.txt
ls -lh samples/repetitive.txt samples/repetitive.txt.rle
```

One-liner: MultiSync creates a `.rle` compressed archive.

### 4. Decompress and verify

```bash
./multisync -d samples/repetitive.txt.rle
cmp samples/repetitive.txt samples/repetitive.txt.out && echo "PERFECT MATCH"
```

One-liner: The decompressed file is byte-for-byte identical to the original.

### 5. Compress multiple files with threads

```bash
./multisync -j 3 samples/repetitive.txt samples/natural.txt samples/alternating.txt
```

One-liner: The `-j` flag controls how many worker threads run at once.

### 6. Create larger files for progress bars

Progress bars are easiest to see with bigger files:

```bash
mkdir -p tests/data

python3 - <<'PY'
from pathlib import Path
data = bytes((i * 37 + 11) % 256 for i in range(16 * 1024 * 1024))
Path("tests/data/signal_big_1.bin").write_bytes(data)
Path("tests/data/signal_big_2.bin").write_bytes(data)
PY
```

One-liner: Large files make the real-time progress monitor visible on camera.

### 7. Show progress bars

```bash
./multisync -p -j 2 tests/data/signal_big_1.bin tests/data/signal_big_2.bin
```

One-liner: The `-p` flag prints live progress bars for active jobs.

### 8. Show adaptive mode

```bash
./multisync -a tests/data/random.bin samples/repetitive.txt
```

If `tests/data/random.bin` does not exist yet, create it:

```bash
mkdir -p tests/data
python3 - <<'PY'
from pathlib import Path
Path("tests/data/random.bin").write_bytes(bytes((i * 37 + 11) % 256 for i in range(64 * 1024)))
PY
```

Then rerun:

```bash
./multisync -a tests/data/random.bin samples/repetitive.txt
```

One-liner: Adaptive mode stores high-entropy data and RLE-compresses repetitive data.

### 9. Print statistics

```bash
./multisync -s -j 3 samples/repetitive.txt samples/natural.txt samples/alternating.txt
```

One-liner: The stats table shows size, ratio, time, and job status.

### 10. Benchmark compression speed

```bash
./multisync --bench samples/repetitive.txt samples/alternating.txt
```

One-liner: Benchmark mode measures compression speed without writing output files.

### 11. Pipe mode

```bash
cat samples/repetitive.txt | ./multisync - > samples/repetitive.pipe.rle
cat samples/repetitive.pipe.rle | ./multisync -d - > samples/repetitive.pipe.out
cmp samples/repetitive.txt samples/repetitive.pipe.out && echo "PIPE PERFECT MATCH"
```

One-liner: Pipe mode lets MultiSync work with standard Unix stdin and stdout.

### 12. Final proof

```bash
make test
```

One-liner: The automated test suite verifies all major behavior.

## Command Reference

```text
Usage:
  ./multisync [options] file1 file2 ...
  cat file | ./multisync [options] - > file.rle

Options:
  -d          Decompress instead of compress
  -j N        Use up to N worker threads
  -o DIR      Write outputs into DIR
  -s          Print a statistics table
  -a          Enable adaptive STORED/RLE selection
  -p          Show progress bars
  -l 1|2|3    Compression level
  --bench     Benchmark compression, no output files
  -h          Show help
```

All options must come before file arguments.

## Common Examples

Compress one file:

```bash
./multisync file.txt
```

Output:

```text
file.txt.rle
```

Decompress one file:

```bash
./multisync -d file.txt.rle
```

Output:

```text
file.txt.out
```

Compress several files with four worker threads:

```bash
./multisync -j 4 a.txt b.txt c.bin d.raw
```

Write outputs into a directory:

```bash
./multisync -o compressed samples/repetitive.txt samples/natural.txt
```

Use adaptive mode:

```bash
./multisync -a file.bin
```

Use maximum compression level:

```bash
./multisync -l 3 file.txt
```

Show stats:

```bash
./multisync -s file.txt
```

Show progress:

```bash
./multisync -p -j 2 big1.bin big2.bin
```

Benchmark:

```bash
./multisync --bench file.txt
```

Pipe compression:

```bash
cat file.txt | ./multisync - > file.txt.rle
```

Pipe decompression:

```bash
cat file.txt.rle | ./multisync -d - > file.txt.out
```

## Flags In Detail

| Flag | Meaning |
|------|---------|
| `-d` | Decompress `.rle` input. The compression level is read from the file header. |
| `-j N` | Set max worker threads. Default is `min(number_of_files, 4)`. |
| `-o DIR` | Send all outputs to one directory. The directory is created if missing. |
| `-s` | Print a summary table after all jobs finish. |
| `-a` | Estimate entropy and store random/high-entropy files instead of RLE-compressing them. |
| `-p` | Start a progress monitor thread that redraws active job progress. |
| `-l 1` | Fast standard RLE. |
| `-l 2` | Default RLE behavior with long-run handling across 255-byte chunks. |
| `-l 3` | Delta preprocessing before RLE; reversed automatically on decompression. |
| `--bench` | Run five compression trials per file and report timing/throughput. |
| `-h` | Print help. |

Pipe mode with `-` is single-job only. It cannot be combined with `-o`, `-s`,
`-p`, or `--bench`, because stdout must remain clean binary data.

## Output Naming

Without `-o`:

| Mode | Input | Output |
|------|-------|--------|
| Compress | `file.txt` | `file.txt.rle` |
| Decompress | `file.txt.rle` | `file.txt.out` |
| Decompress | `data.bin` | `data.bin.out` |

With `-o compressed`:

| Mode | Input | Output |
|------|-------|--------|
| Compress | `samples/repetitive.txt` | `compressed/repetitive.txt.rle` |
| Decompress | `samples/repetitive.txt.rle` | `compressed/repetitive.txt.out` |

## File Format

Every `.rle` file uses a 17-byte header:

```text
Byte(s)   Field                Description
0..3      magic                0x52 0x4C 0x45 0x00 ("RLE\0")
4..11     original_size        uint64_t, little-endian
12..15    crc32                CRC32 of original uncompressed data
16        compression_flags    bit 0: 1 = STORED, 0 = RLE_COMPRESSED
                               bits 1-2: compression level 1..3
17..N     payload              raw bytes for STORED, RLE pairs otherwise
```

RLE payloads store repeated bytes as:

```text
[count: 1 byte] [value: 1 byte]
```

The CRC32 check ensures decompression fails if the archive is corrupted.

## Testing

Run:

```bash
make test
```

The test suite covers:

- Basic compression
- Decompression round trips
- Thread pool behavior
- CRC32 corruption detection
- Statistics output
- SIGINT graceful shutdown
- Output directory handling
- Adaptive STORED/RLE mode
- Progress bars
- Pipe mode
- Compression levels
- Benchmark mode

Expected result:

```text
Results: 36 passed, 0 failed
```

## Troubleshooting

If a command says it cannot read a file, check that the file exists:

```bash
ls -lh path/to/file
```

If progress demo files are missing, create them:

```bash
mkdir -p tests/data
python3 - <<'PY'
from pathlib import Path
data = bytes((i * 37 + 11) % 256 for i in range(16 * 1024 * 1024))
Path("tests/data/signal_big_1.bin").write_bytes(data)
Path("tests/data/signal_big_2.bin").write_bytes(data)
PY
```

If you want to reset generated outputs:

```bash
make clean
```

Then rebuild:

```bash
make
```

## Privacy

This README intentionally avoids personal names, usernames, home-directory
paths, machine names, and course-specific identity details. All commands use
relative paths from the repository root so the project can be cloned and run on
any Linux machine.
