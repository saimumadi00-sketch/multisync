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
    job->status = "FAIL";
    job->in_size = 0;
    job->out_size = 0;

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* ── Read input ── */
    unsigned char *in_buf = NULL;
    long in_size = read_file(job->input_path, &in_buf);
    if (in_size < 0) {
        logger_log(job->job_id, "ERROR: cannot read '%s'", job->input_path);
        goto done;
    }
    job->in_size = (size_t)in_size;
    logger_log(job->job_id, "Read %ld bytes from '%s'", in_size, job->input_path);

    {   /* new scope so we can declare vars after the goto-safe label */

        /* ── Allocate output buffer ── */
        size_t out_max;
        if (job->mode == MODE_COMPRESS) {
            /* RLE worst case: every byte unique → 2 bytes each, plus header */
            out_max = RLE_HEADER_LEN + (size_t)in_size * 2;
        } else {
            /* Read exact original size from the .rle header — no guessing */
            size_t orig = rle_original_size(in_buf, (size_t)in_size);
            if (orig == 0) {
                logger_log(job->job_id,
                           "ERROR: '%s' is not a valid .rle file (bad header)",
                           job->input_path);
                free(in_buf);
                goto done;
            }
            out_max = orig;
        }

        unsigned char *out_buf = malloc(out_max);
        if (!out_buf) {
            logger_log(job->job_id, "ERROR: malloc failed (%zu bytes)", out_max);
            free(in_buf);
            goto done;
        }

        /* ── Compress or decompress ── */
        long out_size;
        if (job->mode == MODE_COMPRESS)
            out_size = rle_compress(in_buf, (size_t)in_size, out_buf, out_max);
        else
            out_size = rle_decompress(in_buf, (size_t)in_size, out_buf, out_max);

        /* Save in_size before freeing — used in log below */
        long saved_in_size = in_size;
        free(in_buf);
        in_buf = NULL;

        if (out_size < 0) {
            if (job->mode == MODE_DECOMPRESS)
                logger_log(job->job_id,
                           "ERROR: decompression failed or CRC mismatch on '%s'",
                           job->input_path);
            else
                logger_log(job->job_id, "ERROR: compression failed on '%s'",
                           job->input_path);
            free(out_buf);
            goto done;
        }
        job->out_size = (size_t)out_size;

        /* ── Write output ── */
        if (write_file(job->output_path, out_buf, (size_t)out_size) < 0) {
            logger_log(job->job_id, "ERROR: cannot write '%s'", job->output_path);
            free(out_buf);
            goto done;
        }
        free(out_buf);

        /* ── Log result ── */
        if (job->mode == MODE_COMPRESS) {
            double ratio = 100.0 * (1.0 - (double)out_size / (double)saved_in_size);
            if (ratio >= 0)
                logger_log(job->job_id,
                           "Compressed '%s' -> '%s' (%.1f%% smaller, %ld -> %ld bytes)",
                           job->input_path, job->output_path,
                           ratio, saved_in_size, out_size);
            else
                logger_log(job->job_id,
                           "Compressed '%s' -> '%s' (%.1f%% larger, %ld -> %ld bytes)",
                           job->input_path, job->output_path,
                           -ratio, saved_in_size, out_size);
        } else {
            logger_log(job->job_id,
                       "Decompressed '%s' -> '%s' (%ld bytes restored)",
                       job->input_path, job->output_path, out_size);
        }

        job->result = 0;
        job->status = "OK";
    }

done:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    job->elapsed_ms = (t_end.tv_sec  - t_start.tv_sec)  * 1000.0
                    + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    return NULL;
}
