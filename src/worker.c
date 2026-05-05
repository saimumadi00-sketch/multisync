#include "worker.h"
#include "rle.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STDIO_CHUNK_SIZE 65536u
#define BENCH_ITERATIONS 5

static int is_stdio_path(const char *path)
{
    return path && strcmp(path, "-") == 0;
}

static int is_stdout_sentinel(const char *path)
{
    return path && path[0] == '\0';
}

/* Read entire file into a malloc'd buffer. Returns byte count or -1. */
static long read_seekable_file(const char *path, unsigned char **buf_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }

    long size = ftell(f);
    if (size <= 0) {
        fclose(f);
        return -1;
    }

    rewind(f);

    unsigned char *buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return -1;
    }

    if ((long)fread(buf, 1, (size_t)size, f) != size) {
        free(buf);
        fclose(f);
        return -1;
    }

    fclose(f);
    *buf_out = buf;
    return size;
}

static long read_stream(FILE *stream, unsigned char **buf_out)
{
    unsigned char *buf = NULL;
    size_t size = 0;
    size_t capacity = 0;

    for (;;) {
        if (size == capacity) {
            size_t next = capacity == 0 ? STDIO_CHUNK_SIZE
                                        : capacity * 2u;
            unsigned char *grown = realloc(buf, next);
            if (!grown) {
                free(buf);
                return -1;
            }
            buf = grown;
            capacity = next;
        }

        size_t got = fread(buf + size, 1, capacity - size, stream);
        size += got;

        if (got == 0) {
            if (ferror(stream)) {
                free(buf);
                return -1;
            }
            break;
        }
    }

    if (size == 0) {
        free(buf);
        return -1;
    }

    *buf_out = buf;
    return (long)size;
}

static long read_input(const char *path, unsigned char **buf_out)
{
    if (is_stdio_path(path))
        return read_stream(stdin, buf_out);
    return read_seekable_file(path, buf_out);
}

/* Write buffer to file or stdout. Returns 0 on success, -1 on failure. */
static int write_output(const char *path, const unsigned char *buf, size_t len)
{
    FILE *f = stdout;

    if (!is_stdout_sentinel(path)) {
        f = fopen(path, "wb");
        if (!f) return -1;
    }

    size_t written = fwrite(buf, 1, len, f);
    if (is_stdout_sentinel(path)) {
        fflush(stdout);
    } else {
        fclose(f);
    }

    return (written == len) ? 0 : -1;
}

static const char *chosen_mode_name(unsigned char mode)
{
    return mode == RLE_MODE_STORED ? "STORED" : "RLE";
}

static void progress_start_if_needed(job_t *job, size_t total)
{
    if (job->progress)
        progress_job_start(job->progress, job->job_id, total);
}

static void progress_finish_if_needed(job_t *job)
{
    if (job->progress)
        progress_job_finish(job->progress, job->job_id);
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

    unsigned char *in_buf = NULL;
    long in_size = read_input(job->input_path, &in_buf);
    if (in_size < 0) {
        logger_log(job->job_id, "ERROR: cannot read '%s'", job->input_path);
        goto done;
    }
    job->in_size = (size_t)in_size;
    logger_log(job->job_id, "Read %ld bytes from '%s'", in_size, job->input_path);

    {
        size_t out_max;
        size_t progress_total = (size_t)in_size;

        if (job->mode == MODE_COMPRESS) {
            out_max = RLE_HEADER_LEN + (size_t)in_size * 2;
        } else {
            size_t orig = rle_original_size(in_buf, (size_t)in_size);
            if (orig == 0) {
                logger_log(job->job_id,
                           "ERROR: '%s' is not a valid .rle file (bad header)",
                           job->input_path);
                free(in_buf);
                goto done;
            }
            out_max = orig;
            progress_total = orig;
        }

        unsigned char *out_buf = malloc(out_max);
        if (!out_buf) {
            logger_log(job->job_id, "ERROR: malloc failed (%zu bytes)", out_max);
            free(in_buf);
            goto done;
        }

        progress_update_ctx_t progress_ctx = {job->progress, job->job_id};
        unsigned char chosen_mode = RLE_MODE_RLE;
        long out_size;

        progress_start_if_needed(job, progress_total);
        if (job->mode == MODE_COMPRESS) {
            out_size = rle_compress(in_buf, (size_t)in_size,
                                    out_buf, out_max,
                                    job->level, job->auto_mode,
                                    &chosen_mode,
                                    progress_rle_callback,
                                    &progress_ctx);
        } else {
            out_size = rle_decompress(in_buf, (size_t)in_size,
                                      out_buf, out_max,
                                      progress_rle_callback,
                                      &progress_ctx);
        }
        progress_finish_if_needed(job);

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

        if (write_output(job->output_path, out_buf, (size_t)out_size) < 0) {
            logger_log(job->job_id, "ERROR: cannot write '%s'", job->output_path);
            free(out_buf);
            goto done;
        }
        free(out_buf);

        if (job->mode == MODE_COMPRESS) {
            double ratio = 100.0 * (1.0 - (double)out_size / (double)saved_in_size);
            logger_log(job->job_id, "Selected mode: %s, level %d",
                       chosen_mode_name(chosen_mode), job->level);
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
    job->elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1000.0
                    + (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
    return NULL;
}

static long long elapsed_us(struct timespec start, struct timespec end)
{
    return (long long)(end.tv_sec - start.tv_sec) * 1000000LL
         + (long long)(end.tv_nsec - start.tv_nsec) / 1000LL;
}

int bench_job(job_t *job)
{
    unsigned char *in_buf = NULL;
    unsigned char *out_buf = NULL;
    long in_size = read_input(job->input_path, &in_buf);

    job->result = -1;
    job->status = "FAIL";
    job->in_size = 0;
    job->out_size = 0;
    job->bench_min_us = 0;
    job->bench_max_us = 0;
    job->bench_avg_us = 0;
    job->bench_mbps = 0.0;

    if (in_size < 0)
        return -1;

    job->in_size = (size_t)in_size;
    size_t out_max = RLE_HEADER_LEN + (size_t)in_size * 2;
    out_buf = malloc(out_max);
    if (!out_buf) {
        free(in_buf);
        return -1;
    }

    long long min_us = 0;
    long long max_us = 0;
    long long total_us = 0;

    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        struct timespec start, end;
        unsigned char chosen_mode = RLE_MODE_RLE;

        clock_gettime(CLOCK_MONOTONIC, &start);
        long out_size = rle_compress(in_buf, (size_t)in_size,
                                     out_buf, out_max,
                                     job->level, job->auto_mode,
                                     &chosen_mode, NULL, NULL);
        clock_gettime(CLOCK_MONOTONIC, &end);

        if (out_size < 0) {
            free(out_buf);
            free(in_buf);
            return -1;
        }

        long long us = elapsed_us(start, end);
        if (i == 0 || us < min_us)
            min_us = us;
        if (i == 0 || us > max_us)
            max_us = us;
        total_us += us;
        job->out_size = (size_t)out_size;
    }

    job->bench_min_us = min_us;
    job->bench_max_us = max_us;
    job->bench_avg_us = total_us / BENCH_ITERATIONS;
    if (job->bench_avg_us > 0) {
        double mib = (double)job->in_size / (1024.0 * 1024.0);
        job->bench_mbps = mib / ((double)job->bench_avg_us / 1000000.0);
    }

    job->result = 0;
    job->status = "OK";
    free(out_buf);
    free(in_buf);
    return 0;
}
