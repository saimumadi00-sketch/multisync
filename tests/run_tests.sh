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
check "T9: long run compresses to expected 18 bytes" \
    test "$LONGRUN_RLE_SIZE" -eq 18

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

echo "────────────────────────────────────────"
echo "Results: $PASS passed, $FAIL failed"
echo ""
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
