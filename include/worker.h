#ifndef WORKER_H
#define WORKER_H

typedef enum { MODE_COMPRESS, MODE_DECOMPRESS } op_mode_t;

/* Job descriptor passed to each worker thread */
typedef struct {
    const char *input_path;   /* path to input file          */
    const char *output_path;  /* path to output file         */
    op_mode_t   mode;         /* compress or decompress      */
    int         job_id;       /* index (for logging)         */
    int         result;       /* 0 = success, -1 = failure   */
    double      elapsed_ms;   /* wall-clock time for this job*/
} job_t;

/* Thread entry point. Argument must be a job_t*. */
void *worker_run(void *arg);

#endif /* WORKER_H */
