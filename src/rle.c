#include "rle.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

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

double entropy_estimate(const unsigned char *src, size_t len)
{
    size_t counts[256] = {0};
    double entropy = 0.0;

    if (!src || len == 0)
        return 0.0;

    for (size_t i = 0; i < len; i++)
        counts[src[i]]++;

    for (int i = 0; i < 256; i++) {
        if (counts[i] > 0) {
            double p = (double)counts[i] / (double)len;
            entropy -= p * log2(p);
        }
    }

    return entropy;
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

static uint32_t read_uint32_le(const unsigned char *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++)
        v |= ((uint32_t)p[i]) << (8 * i);
    return v;
}

static uint64_t read_uint64_le(const unsigned char *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)p[i]) << (8 * i);
    return v;
}

static int check_magic(const unsigned char *src)
{
    return src[0] == RLE_MAGIC_0 &&
           src[1] == RLE_MAGIC_1 &&
           src[2] == RLE_MAGIC_2 &&
           src[3] == RLE_MAGIC_3;
}

static unsigned char make_flags(unsigned char mode, int level)
{
    unsigned char flags = (unsigned char)((level << RLE_LEVEL_SHIFT) &
                                          RLE_LEVEL_MASK);
    if (mode == RLE_MODE_STORED)
        flags |= RLE_MODE_STORED;
    return flags;
}

static int level_from_flags(unsigned char flags)
{
    return (int)((flags & RLE_LEVEL_MASK) >> RLE_LEVEL_SHIFT);
}

static void write_header(unsigned char *dst, uint64_t original_size,
                         uint32_t original_crc, unsigned char mode, int level)
{
    dst[0] = RLE_MAGIC_0;
    dst[1] = RLE_MAGIC_1;
    dst[2] = RLE_MAGIC_2;
    dst[3] = RLE_MAGIC_3;
    write_uint64_le(dst + 4, original_size);
    write_uint32_le(dst + 12, original_crc);
    dst[16] = make_flags(mode, level);
}

static int valid_level(int level)
{
    return level >= RLE_MIN_LEVEL && level <= RLE_MAX_LEVEL;
}

static void report_progress(rle_progress_fn progress, void *ctx,
                            size_t processed)
{
    if (progress)
        progress(ctx, processed);
}

static void delta_encode(const unsigned char *src, unsigned char *dst,
                         size_t len)
{
    if (len == 0)
        return;

    dst[0] = src[0];
    for (size_t i = 1; i < len; i++)
        dst[i] = (unsigned char)(src[i] - src[i - 1]);
}

static void delta_decode_in_place(unsigned char *buf, size_t len)
{
    for (size_t i = 1; i < len; i++)
        buf[i] = (unsigned char)(buf[i] + buf[i - 1]);
}

static long rle_encode_payload(const unsigned char *src, size_t len,
                               unsigned char *dst, size_t dst_len,
                               rle_progress_fn progress, void *progress_ctx)
{
    size_t i = 0;
    size_t out = RLE_HEADER_LEN;
    size_t last_report = 0;

    while (i < len) {
        unsigned char val = src[i];
        unsigned char count = 1;

        while (i + count < len && src[i + count] == val && count < 255)
            count++;

        if (out + 2 > dst_len)
            return -1;

        dst[out++] = count;
        dst[out++] = val;
        i += count;

        if (i - last_report >= 65536u || i == len) {
            report_progress(progress, progress_ctx, i);
            last_report = i;
        }
    }

    return (long)out;
}

size_t rle_original_size(const unsigned char *src, size_t len)
{
    if (!src || len < RLE_HEADER_LEN) return 0;
    if (!check_magic(src))            return 0;
    return (size_t)read_uint64_le(src + 4);
}

long rle_compress(const unsigned char *src, size_t len,
                  unsigned char *dst, size_t dst_len,
                  int level, int auto_mode, unsigned char *chosen_mode,
                  rle_progress_fn progress, void *progress_ctx)
{
    unsigned char mode = RLE_MODE_RLE;
    const unsigned char *rle_input = src;
    unsigned char *delta_buf = NULL;
    long result;

    if (!src || !dst || len == 0)             return -1;
    if (!valid_level(level))                  return -1;
    if (dst_len < RLE_HEADER_LEN + 2 * len)   return -1;

    if (auto_mode) {
        size_t sample_len = len < RLE_AUTO_SAMPLE_SIZE
                          ? len
                          : RLE_AUTO_SAMPLE_SIZE;
        if (entropy_estimate(src, sample_len) > RLE_ENTROPY_THRESHOLD)
            mode = RLE_MODE_STORED;
    }

    if (chosen_mode)
        *chosen_mode = mode;

    write_header(dst, (uint64_t)len, crc32(src, len), mode, level);

    if (mode == RLE_MODE_STORED) {
        if (dst_len < RLE_HEADER_LEN + len)
            return -1;
        memcpy(dst + RLE_HEADER_LEN, src, len);
        report_progress(progress, progress_ctx, len);
        return (long)(RLE_HEADER_LEN + len);
    }

    if (level == 3) {
        delta_buf = malloc(len);
        if (!delta_buf)
            return -1;
        delta_encode(src, delta_buf, len);
        rle_input = delta_buf;
    }

    result = rle_encode_payload(rle_input, len, dst, dst_len,
                                progress, progress_ctx);
    free(delta_buf);
    return result;
}

long rle_decompress(const unsigned char *src, size_t len,
                    unsigned char *dst, size_t dst_len,
                    rle_progress_fn progress, void *progress_ctx)
{
    uint64_t expected;
    uint32_t expected_crc;
    unsigned char flags;
    int stored;
    int level;

    if (!src || !dst || len < RLE_HEADER_LEN) return -1;
    if (!check_magic(src))                    return -1;

    expected = read_uint64_le(src + 4);
    expected_crc = read_uint32_le(src + 12);
    flags = src[16];
    stored = (flags & RLE_MODE_STORED) != 0;
    level = level_from_flags(flags);

    if (!valid_level(level))                  return -1;
    if (dst_len < (size_t)expected)           return -1;

    if (stored) {
        size_t pay_len = len - RLE_HEADER_LEN;
        if (pay_len != (size_t)expected)
            return -1;
        memcpy(dst, src + RLE_HEADER_LEN, pay_len);
        report_progress(progress, progress_ctx, pay_len);
        if (crc32(dst, pay_len) != expected_crc)
            return -1;
        return (long)pay_len;
    }

    {
        size_t pay_len = len - RLE_HEADER_LEN;
        const unsigned char *pay = src + RLE_HEADER_LEN;
        unsigned char *target = dst;
        unsigned char *delta_buf = NULL;
        size_t i = 0;
        size_t out = 0;
        size_t last_report = 0;

        if (pay_len % 2 != 0)
            return -1;

        if (level == 3) {
            delta_buf = malloc((size_t)expected);
            if (!delta_buf)
                return -1;
            target = delta_buf;
        }

        while (i < pay_len) {
            unsigned char count = pay[i++];
            unsigned char val = pay[i++];

            if (out + count > dst_len || out + count > (size_t)expected) {
                free(delta_buf);
                return -1;
            }

            memset(target + out, val, count);
            out += count;

            if (out - last_report >= 65536u || out == (size_t)expected) {
                report_progress(progress, progress_ctx, out);
                last_report = out;
            }
        }

        if (out != (size_t)expected) {
            free(delta_buf);
            return -1;
        }

        if (level == 3) {
            delta_decode_in_place(delta_buf, out);
            memcpy(dst, delta_buf, out);
            free(delta_buf);
        }

        if (crc32(dst, out) != expected_crc)
            return -1;

        return (long)out;
    }
}
