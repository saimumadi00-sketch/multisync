#include "rle.h"
#include <stdio.h>

/*
 * RLE format (compress):
 *   For each run of identical bytes:
 *     byte 1: count (1-255)
 *     byte 2: the repeated byte value
 *
 * Example: AAABBC -> 03 41 02 42 01 43
 */

long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst, size_t dst_len)
{
    if (!src || !dst || len == 0) return -1;

    size_t i = 0, out = 0;

    while (i < len) {
        unsigned char val   = src[i];
        unsigned char count = 1;

        /* Count consecutive identical bytes (max 255 per run) */
        while (i + count < len && src[i + count] == val && count < 255)
            count++;

        if (out + 2 > dst_len) return -1; /* output buffer too small */

        dst[out++] = count;
        dst[out++] = val;
        i += count;
    }

    return (long)out;
}

long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst, size_t dst_len)
{
    if (!src || !dst || len == 0) return -1;
    if (len % 2 != 0) return -1; /* must be pairs */

    size_t i = 0, out = 0;

    while (i < len) {
        unsigned char count = src[i++];
        unsigned char val   = src[i++];

        if (out + count > dst_len) return -1;

        for (unsigned char k = 0; k < count; k++)
            dst[out++] = val;
    }

    return (long)out;
}
