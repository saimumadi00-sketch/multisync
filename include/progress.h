#ifndef PROGRESS_H
#define PROGRESS_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
    size_t processed;
    size_t total;
    int active;
    int done;
} progress_slot_t;

typedef struct {
    progress_slot_t *slots;
    int count;
    int stop;
    int lines_printed;
    pthread_rwlock_t lock;
} progress_t;

typedef struct {
    progress_t *progress;
    int job_id;
} progress_update_ctx_t;

int  progress_init(progress_t *progress, int count);
void progress_destroy(progress_t *progress);
void progress_job_start(progress_t *progress, int job_id, size_t total);
void progress_update(progress_t *progress, int job_id, size_t processed);
void progress_job_finish(progress_t *progress, int job_id);
void progress_request_stop(progress_t *progress);
void *progress_monitor_run(void *arg);
void progress_rle_callback(void *ctx, size_t processed);

#endif /* PROGRESS_H */
