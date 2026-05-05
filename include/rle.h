#ifndef RLE_H
#define RLE_H

#include <stddef.h>
#include <stdint.h>

/*
 * .rle file format, version 4:
 *   [0..3]   magic:              0x52 0x4C 0x45 0x00  ("RLE\0")
 *   [4..11]  original_size:      uint64_t, little-endian
 *   [12..15] crc32:              CRC32 of original data, little-endian
 *   [16]     compression_flags:  bit 0    = 1 STORED, 0 RLE_COMPRESSED
 *                                bits 1-2 = compression level 1..3
 *   [17..N]  payload:            raw bytes for STORED, or RLE pairs
 *
 * Header byte 16 unifies adaptive stored mode and compression levels:
 *   flags & RLE_MODE_STORED       -> payload is uncompressed original bytes
 *   (flags & RLE_LEVEL_MASK) >> 1 -> level used for RLE payloads
 */

#define RLE_MAGIC_0    0x52u  /* 'R' */
#define RLE_MAGIC_1    0x4Cu  /* 'L' */
#define RLE_MAGIC_2    0x45u  /* 'E' */
#define RLE_MAGIC_3    0x00u

#define RLE_HEADER_LEN 17

#define RLE_MODE_RLE    0x00u
#define RLE_MODE_STORED 0x01u

#define RLE_LEVEL_SHIFT 1u
#define RLE_LEVEL_MASK  0x06u
#define RLE_MIN_LEVEL   1
#define RLE_MAX_LEVEL   3
#define RLE_DEFAULT_LEVEL 2

#define RLE_AUTO_SAMPLE_SIZE 4096u
#define RLE_ENTROPY_THRESHOLD 7.2

typedef void (*rle_progress_fn)(void *ctx, size_t processed);

/* Estimate Shannon entropy over len bytes. Returns 0.0 for empty input. */
double entropy_estimate(const unsigned char *src, size_t len);

/* Compress src into dst using level 1..3. If auto_mode is non-zero,
 * high-entropy data is stored raw instead of RLE-compressed.
 * chosen_mode receives RLE_MODE_STORED or RLE_MODE_RLE when non-NULL.
 * dst must have at least RLE_HEADER_LEN + 2*len bytes.
 * Returns total bytes written (header + payload), or -1 on error. */
long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst, size_t dst_len,
                  int level, int auto_mode, unsigned char *chosen_mode,
                  rle_progress_fn progress, void *progress_ctx);

/* Decompress src into dst. The stored level is read from the header; callers
 * do not pass -l for decompression.
 * dst must be at least rle_original_size(src, len) bytes.
 * Returns bytes written, or -1 on error. */
long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst, size_t dst_len,
                    rle_progress_fn progress, void *progress_ctx);

/* Read the stored original size from a compressed buffer's header.
 * Returns 0 if the magic bytes are wrong or the buffer is too short. */
size_t rle_original_size(const unsigned char *src, size_t len);

#endif /* RLE_H */
