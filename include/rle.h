#ifndef RLE_H
#define RLE_H

#include <stddef.h>

/* Compress src (len bytes) into dst using Run-Length Encoding.
 * Returns number of bytes written to dst, or -1 on error.
 * dst must be pre-allocated with at least 2*len bytes. */
long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst, size_t dst_len);

/* Decompress src (len bytes) into dst.
 * Returns number of bytes written to dst, or -1 on error. */
long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst, size_t dst_len);

#endif /* RLE_H */
