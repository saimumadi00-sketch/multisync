#!/usr/bin/env bash
set -e
PASS=0; FAIL=0
BIN=./multisync
DATA=tests/data

mkdir -p "$DATA"

# ── Generate test data ──────────────────────────────────────────────
printf 'Hello, World!\n'                          > "$DATA/hello.txt"
printf 'AAAAAAAAABBBBBBBBCCCCCCCC'                > "$DATA/repeat.txt"
dd if=/dev/urandom bs=1024 count=64 2>/dev/null   > "$DATA/random.bin"
printf ''                                          > "$DATA/empty.txt"
printf 'Z'                                         > "$DATA/single.txt"
printf 'NOT_A_REAL_RLE_FILE'                       > "$DATA/corrupt.rle"
printf 'pool-alpha-data'                           > "$DATA/pool_a.txt"
printf 'pool-beta-data'                            > "$DATA/pool_b.txt"
printf 'pool-gamma-data'                           > "$DATA/pool_c.txt"
printf 'output-directory-data'                     > "$DATA/outsrc.txt"
python3 -c "print('A'*500 + 'B'*500, end='')"      > "$DATA/large.txt"
python3 -c "print('A'*700, end='')"                > "$DATA/longrun.txt"
python3 -c "print('AB'*512, end='')"               > "$DATA/alternating.txt"
python3 -c "from pathlib import Path; Path('$DATA/binary_pattern.bin').write_bytes(bytes([0])*300 + bytes([255])*300 + bytes(range(64))*4)"

check() {
    local name="$1"; shift
    if "$@" > /dev/null 2>&1; then
        echo "  [PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "  [FAIL] $name"
        FAIL=$((FAIL + 1))
    fi
}

echo ""
echo "Running MultiSync test suite..."
echo "────────────────────────────────────────"

# T1 – basic compress
check "T1: compress hello.txt" \
    $BIN "$DATA/hello.txt"
check "T1: output file exists" \
    test -f "$DATA/hello.txt.rle"

# T2 – roundtrip: compress then decompress, compare
$BIN "$DATA/repeat.txt" > /dev/null 2>&1
$BIN -d "$DATA/repeat.txt.rle" > /dev/null 2>&1
check "T2: roundtrip repeat.txt matches original" \
    cmp "$DATA/repeat.txt" "$DATA/repeat.txt.out"

# T3 – multi-file compress
check "T3: compress multiple files" \
    $BIN "$DATA/hello.txt" "$DATA/repeat.txt" "$DATA/large.txt"

# T4 – large file roundtrip
$BIN "$DATA/large.txt" > /dev/null 2>&1
$BIN -d "$DATA/large.txt.rle" > /dev/null 2>&1
check "T4: large file roundtrip matches" \
    cmp "$DATA/large.txt" "$DATA/large.txt.out"

# T5 – parallel flag
check "T5: -j 2 flag works" \
    $BIN -j 2 "$DATA/hello.txt" "$DATA/repeat.txt" "$DATA/large.txt"

# T6 – empty file should fail gracefully (exit 1, no crash)
set +e
$BIN "$DATA/empty.txt" > /dev/null 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ]; then
    echo "  [PASS] T6: empty file handled gracefully (exit $CODE)"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T6: expected failure on empty file"
    FAIL=$((FAIL + 1))
fi

# T7 – bad file path
set +e
$BIN /nonexistent/file.txt > /dev/null 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ]; then
    echo "  [PASS] T7: bad path handled gracefully"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T7: expected failure on bad path"
    FAIL=$((FAIL + 1))
fi

# T8 – single-byte file roundtrip
$BIN "$DATA/single.txt" > /dev/null 2>&1
$BIN -d "$DATA/single.txt.rle" > /dev/null 2>&1
check "T8: single-byte file roundtrip matches" \
    cmp "$DATA/single.txt" "$DATA/single.txt.out"

# T9 – long run should split into multiple 255-byte RLE pairs
$BIN "$DATA/longrun.txt" > /dev/null 2>&1
$BIN -d "$DATA/longrun.txt.rle" > /dev/null 2>&1
check "T9: long run roundtrip matches" \
    cmp "$DATA/longrun.txt" "$DATA/longrun.txt.out"
LONGRUN_RLE_SIZE=$(wc -c < "$DATA/longrun.txt.rle")
check "T9: long run compresses to expected 22 bytes" \
    test "$LONGRUN_RLE_SIZE" -eq 22

# T10 – binary pattern data with NUL and 0xFF bytes
$BIN "$DATA/binary_pattern.bin" > /dev/null 2>&1
$BIN -d "$DATA/binary_pattern.bin.rle" > /dev/null 2>&1
check "T10: binary pattern roundtrip matches" \
    cmp "$DATA/binary_pattern.bin" "$DATA/binary_pattern.bin.out"

# T11 – alternating data expands but must still roundtrip correctly
$BIN "$DATA/alternating.txt" > /dev/null 2>&1
$BIN -d "$DATA/alternating.txt.rle" > /dev/null 2>&1
check "T11: alternating data roundtrip matches" \
    cmp "$DATA/alternating.txt" "$DATA/alternating.txt.out"

# T12 – corrupt .rle should fail gracefully
set +e
$BIN -d "$DATA/corrupt.rle" > /dev/null 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ]; then
    echo "  [PASS] T12: corrupt .rle handled gracefully"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T12: expected failure on corrupt .rle"
    FAIL=$((FAIL + 1))
fi

# T13 – thread pool handles more jobs than worker threads
check "T13: thread pool runs queued jobs with -j 2" \
    $BIN -j 2 "$DATA/pool_a.txt" "$DATA/pool_b.txt" "$DATA/pool_c.txt" "$DATA/hello.txt" "$DATA/repeat.txt"
check "T13: queued job output exists" \
    test -f "$DATA/pool_c.txt.rle"

# T14 – thread pool preserves per-job failure without stopping successful jobs
set +e
$BIN -j 2 "$DATA/pool_a.txt" /nonexistent/threadpool.txt "$DATA/pool_b.txt" > /dev/null 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ] && [ -f "$DATA/pool_a.txt.rle" ] && [ -f "$DATA/pool_b.txt.rle" ]; then
    echo "  [PASS] T14: thread pool reports failed job and completes valid jobs"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T14: expected mixed success/failure from thread pool"
    FAIL=$((FAIL + 1))
fi

# T15 – CRC32 header supports successful integrity-checked roundtrip
$BIN "$DATA/binary_pattern.bin" > /dev/null 2>&1
$BIN -d "$DATA/binary_pattern.bin.rle" > /dev/null 2>&1
check "T15: CRC32 roundtrip passes for binary data" \
    cmp "$DATA/binary_pattern.bin" "$DATA/binary_pattern.bin.out"

# T16 – CRC32 mismatch is detected during decompression
$BIN "$DATA/repeat.txt" > /dev/null 2>&1
cp "$DATA/repeat.txt.rle" "$DATA/repeat_bad_crc.rle"
python3 -c "from pathlib import Path; p=Path('$DATA/repeat_bad_crc.rle'); b=bytearray(p.read_bytes()); b[12] ^= 0xFF; p.write_bytes(b)"
set +e
$BIN -d "$DATA/repeat_bad_crc.rle" > "$DATA/repeat_bad_crc.log" 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ] && grep -q "CRC" "$DATA/repeat_bad_crc.log"; then
    echo "  [PASS] T16: CRC32 mismatch fails with error"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T16: expected CRC32 mismatch failure"
    FAIL=$((FAIL + 1))
fi

# T17 – -s prints the requested statistics table and aggregate rows
$BIN -s "$DATA/hello.txt" "$DATA/repeat.txt" > "$DATA/stats_success.log" 2>&1
if grep -q "File | Original | Compressed | Ratio | Time(ms) | Status" "$DATA/stats_success.log" &&
   grep -q "total bytes saved" "$DATA/stats_success.log" &&
   grep -q "average ratio" "$DATA/stats_success.log"; then
    echo "  [PASS] T17: -s prints statistics report"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T17: expected statistics report"
    FAIL=$((FAIL + 1))
fi

# T18 – -s still reports failed jobs
set +e
$BIN -s /nonexistent/stats.txt > "$DATA/stats_failure.log" 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ] && grep -q "FAIL" "$DATA/stats_failure.log"; then
    echo "  [PASS] T18: -s reports failed job status"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T18: expected failed status in statistics report"
    FAIL=$((FAIL + 1))
fi

# T19 – normal run does not report interruption
$BIN -j 1 "$DATA/pool_a.txt" "$DATA/pool_b.txt" > "$DATA/no_sigint.log" 2>&1
if ! grep -q "Interrupted" "$DATA/no_sigint.log"; then
    echo "  [PASS] T19: non-interrupted run completes normally"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T19: unexpected interruption message"
    FAIL=$((FAIL + 1))
fi

# T20 – SIGINT stops dispatching new jobs and reports cancellations
python3 -c "from pathlib import Path; base=Path('$DATA'); data=bytes((i * 37 + 11) % 256 for i in range(16 * 1024 * 1024)); [base.joinpath(f'signal_big_{n}.bin').write_bytes(data) for n in range(1, 4)]"
set +e
$BIN -j 1 "$DATA/signal_big_1.bin" "$DATA/signal_big_2.bin" "$DATA/signal_big_3.bin" > "$DATA/sigint.log" 2>&1 &
PID=$!
sleep 0.05
kill -INT "$PID" 2>/dev/null
wait "$PID"
CODE=$?
set -e
if [ "$CODE" -ne 0 ] && grep -q "Interrupted" "$DATA/sigint.log" &&
   grep -q "jobs cancelled" "$DATA/sigint.log"; then
    echo "  [PASS] T20: SIGINT graceful shutdown reports cancellations"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T20: expected SIGINT cancellation report"
    FAIL=$((FAIL + 1))
fi

# T21 – -o creates an output directory and writes outputs there
rm -rf "$DATA/outdir"
check "T21: -o creates directory and writes compressed output" \
    $BIN -o "$DATA/outdir" "$DATA/outsrc.txt"
check "T21: output is inside requested directory" \
    test -f "$DATA/outdir/outsrc.txt.rle"

# T22 – -o fails clearly if the output path is a file
printf 'not a directory' > "$DATA/not_a_dir"
set +e
$BIN -o "$DATA/not_a_dir" "$DATA/outsrc.txt" > "$DATA/not_a_dir.log" 2>&1
CODE=$?
set -e
if [ "$CODE" -ne 0 ] && grep -q "not a directory" "$DATA/not_a_dir.log"; then
    echo "  [PASS] T22: -o rejects non-directory path"
    PASS=$((PASS + 1))
else
    echo "  [FAIL] T22: expected -o non-directory failure"
    FAIL=$((FAIL + 1))
fi

echo "────────────────────────────────────────"
echo "Results: $PASS passed, $FAIL failed"
echo ""
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
