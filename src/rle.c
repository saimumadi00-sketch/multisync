#include "rle.h"

#include <string.h>

/* ── Header helpers ─────────────────────────────────────────────────── */

static uint32_t crc32(const unsigned char *src, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= src[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFu;
}

static void write_uint32_le(unsigned char *p, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char)((value >> (8 * i)) & 0xFFu);
}

static void write_uint64_le(unsigned char *p, uint64_t value)
{
    for (int i = 0; i < 8; i++)
        p[i] = (unsigned char)((value >> (8 * i)) & 0xFFu);
}

static void write_header(unsigned char *dst, uint64_t original_size,
                         uint32_t original_crc)
{
    /* Magic */
    dst[0] = RLE_MAGIC_0;
    dst[1] = RLE_MAGIC_1;
    dst[2] = RLE_MAGIC_2;
    dst[3] = RLE_MAGIC_3;

    write_uint64_le(dst + 4, original_size);
    write_uint32_le(dst + 12, original_crc);
}

static int check_magic(const unsigned char *src)
{
    return src[0] == RLE_MAGIC_0 &&
           src[1] == RLE_MAGIC_1 &&
           src[2] == RLE_MAGIC_2 &&
           src[3] == RLE_MAGIC_3;
}

static uint64_t read_uint64_le(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

static uint32_t read_uint32_le(const unsigned char *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= ((uint32_t)p[i]) << (8 * i);
    return v;
}

/* ── Public API ─────────────────────────────────────────────────────── */

size_t rle_original_size(const unsigned char *src, size_t len)
{
    if (!src || len < RLE_HEADER_LEN) return 0;
    if (!check_magic(src))            return 0;
    return (size_t)read_uint64_le(src + 4);
}

long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst,  size_t dst_len)
{
    if (!src || !dst || len == 0)              return -1;
    if (dst_len < RLE_HEADER_LEN + 2 * len)   return -1;

    write_header(dst, (uint64_t)len, crc32(src, len));

    size_t i = 0;
    size_t out = RLE_HEADER_LEN;   /* payload starts after header */

    while (i < len) {
        unsigned char val   = src[i];
        unsigned char count = 1;

        while (i + count < len && src[i + count] == val && count < 255)
            count++;

        if (out + 2 > dst_len) return -1;
        dst[out++] = count;
        dst[out++] = val;
        i += count;
    }

    return (long)out;
}

long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst,  size_t dst_len)
{
    if (!src || !dst || len < RLE_HEADER_LEN) return -1;
    if (!check_magic(src))                    return -1;

    uint64_t expected = read_uint64_le(src + 4);
    uint32_t expected_crc = read_uint32_le(src + 12);
    if (dst_len < (size_t)expected)           return -1;

    /* Payload starts after the header */
    size_t pay_len = len - RLE_HEADER_LEN;
    if (pay_len % 2 != 0) return -1;  /* must be (count, value) pairs */

    const unsigned char *pay = src + RLE_HEADER_LEN;
    size_t i = 0, out = 0;

    while (i < pay_len) {
        unsigned char count = pay[i++];
        unsigned char val   = pay[i++];

        if (out + count > dst_len) return -1;
        memset(dst + out, val, count);
        out += count;
    }

    /* Sanity: decoded size must match what the header promised */
    if (out != (size_t)expected) return -1;
    if (crc32(dst, out) != expected_crc) return -1;

    return (long)out;
}
