#include "worker.h"
#include "rle.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Read entire file into a malloc'd buffer. Returns byte count or -1. */
static long read_file(const char *path, unsigned char **buf_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size <= 0) { fclose(f); return -1; }

    unsigned char *buf = malloc((size_t)size);
    if (!buf) { fclose(f); return -1; }

    if ((long)fread(buf, 1, (size_t)size, f) != size) {
        free(buf); fclose(f); return -1;
    }

    fclose(f);
    *buf_out = buf;
    return size;
}

/* Write buffer to file. Returns 0 on success, -1 on failure. */
static int write_file(const char *path, const unsigned char *buf, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    size_t written = fwrite(buf, 1, len, f);
    fclose(f);
    return (written == len) ? 0 : -1;
}

void *worker_run(void *arg)
{
    job_t *job = (job_t *)arg;
    job->result = -1;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* ── Read input ── */
    unsigned char *in_buf = NULL;
    long in_size = read_file(job->input_path, &in_buf);
    if (in_size < 0) {
        logger_log(job->job_id, "ERROR: cannot read '%s'", job->input_path);
        goto done;
    }
    logger_log(job->job_id, "Read %ld bytes from '%s'", in_size, job->input_path);

    /* ── Allocate output buffer ── */
    size_t out_max = (job->mode == MODE_COMPRESS)
                     ? (size_t)in_size * 2   /* RLE worst case */
                     : (size_t)in_size * 128; /* decompress upper bound */

    unsigned char *out_buf = malloc(out_max);
    if (!out_buf) {
        logger_log(job->job_id, "ERROR: malloc failed");
        free(in_buf);
        goto done;
    }

    /* ── Compress or Decompress ── */
    long out_size;
    if (job->mode == MODE_COMPRESS) {
        out_size = rle_compress(in_buf, (size_t)in_size, out_buf, out_max);
    } else {
        out_size = rle_decompress(in_buf, (size_t)in_size, out_buf, out_max);
    }

    free(in_buf);

    if (out_size < 0) {
        logger_log(job->job_id, "ERROR: %s failed on '%s'",
                   job->mode == MODE_COMPRESS ? "compression" : "decompression",
                   job->input_path);
        free(out_buf);
        goto done;
    }

    /* ── Write output ── */
    if (write_file(job->output_path, out_buf, (size_t)out_size) < 0) {
        logger_log(job->job_id, "ERROR: cannot write '%s'", job->output_path);
        free(out_buf);
        goto done;
    }
    free(out_buf);

    /* ── Log result ── */
    double ratio = (job->mode == MODE_COMPRESS)
                   ? (100.0 * (1.0 - (double)out_size / (double)in_size))
                   : 0.0;

    if (job->mode == MODE_COMPRESS)
        logger_log(job->job_id, "Compressed '%s' -> '%s' (%.1f%% smaller, %ld -> %ld bytes)",
                   job->input_path, job->output_path, ratio, in_size, out_size);
    else
        logger_log(job->job_id, "Decompressed '%s' -> '%s' (%ld bytes)",
                   job->input_path, job->output_path, out_size);

    job->result = 0;

done:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    job->elapsed_ms = (t_end.tv_sec  - t_start.tv_sec)  * 1000.0
                    + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    return NULL;
}
