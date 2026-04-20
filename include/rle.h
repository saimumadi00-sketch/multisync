#ifndef RLE_H
#define RLE_H

#include <stddef.h>
#include <stdint.h>

/*
 * .rle file format:
 *   [4 bytes] magic:         0x52 0x4C 0x45 0x00  ("RLE\0")
 *   [8 bytes] original_size: uint64_t, little-endian
 *   [N bytes] rle_pairs:     (count, value) pairs
 *
 * Storing original_size in the header lets decompression allocate
 * exactly the right buffer instead of guessing with a multiplier.
 */

#define RLE_MAGIC_0    0x52u  /* 'R' */
#define RLE_MAGIC_1    0x4Cu  /* 'L' */
#define RLE_MAGIC_2    0x45u  /* 'E' */
#define RLE_MAGIC_3    0x00u
#define RLE_HEADER_LEN 12     /* 4 magic + 8 original_size */

/* Compress src into dst.
 * dst must have at least RLE_HEADER_LEN + 2*len bytes.
 * Returns total bytes written (header + payload), or -1 on error. */
long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst,  size_t dst_len);

/* Decompress src into dst.
 * dst must be at least rle_original_size(src, len) bytes.
 * Returns bytes written, or -1 on error. */
long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst,  size_t dst_len);

/* Read the stored original size from a compressed buffer's header.
 * Returns 0 if the magic bytes are wrong or the buffer is too short. */
size_t rle_original_size(const unsigned char *src, size_t len);

#endif /* RLE_H */
