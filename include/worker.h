#ifndef WORKER_H
#define WORKER_H

#include "progress.h"

#include <stddef.h>

typedef enum { MODE_COMPRESS, MODE_DECOMPRESS } op_mode_t;

/* Job descriptor passed to each worker thread */
typedef struct {
    const char *input_path;   /* path to input file          */
    const char *output_path;  /* path to output file         */
    op_mode_t   mode;         /* compress or decompress      */
    int         job_id;       /* index (for logging)         */
    int         result;       /* 0 = success, -1 = failure   */
    double      elapsed_ms;   /* wall-clock time for this job*/
    size_t      in_size;      /* bytes read from input       */
    size_t      out_size;     /* bytes written to output     */
    const char *status;       /* PENDING, OK, FAIL, CANCELLED*/
    int         auto_mode;    /* adaptive entropy selection  */
    int         level;        /* compression level 1..3      */
    progress_t *progress;     /* optional progress tracker   */
    long long   bench_min_us; /* benchmark minimum time      */
    long long   bench_max_us; /* benchmark maximum time      */
    long long   bench_avg_us; /* benchmark average time      */
    double      bench_mbps;   /* benchmark throughput        */
} job_t;

/* Thread entry point. Argument must be a job_t*. */
void *worker_run(void *arg);

/* Run a five-iteration compression benchmark. */
int bench_job(job_t *job);

#endif /* WORKER_H */
