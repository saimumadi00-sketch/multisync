#include "progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROGRESS_BAR_WIDTH 16

int progress_init(progress_t *progress, int count)
{
    if (!progress || count < 1)
        return -1;

    memset(progress, 0, sizeof(*progress));
    progress->slots = calloc((size_t)count, sizeof(*progress->slots));
    if (!progress->slots)
        return -1;

    if (pthread_rwlock_init(&progress->lock, NULL) != 0) {
        free(progress->slots);
        memset(progress, 0, sizeof(*progress));
        return -1;
    }

    progress->count = count;
    return 0;
}

void progress_destroy(progress_t *progress)
{
    if (!progress || !progress->slots)
        return;

    pthread_rwlock_destroy(&progress->lock);
    free(progress->slots);
    memset(progress, 0, sizeof(*progress));
}

void progress_job_start(progress_t *progress, int job_id, size_t total)
{
    if (!progress || job_id < 0 || job_id >= progress->count)
        return;

    pthread_rwlock_wrlock(&progress->lock);
    progress->slots[job_id].total = total;
    progress->slots[job_id].active = 1;
    progress->slots[job_id].done = 0;
    __atomic_store_n(&progress->slots[job_id].processed, 0u,
                     __ATOMIC_RELAXED);
    pthread_rwlock_unlock(&progress->lock);
}

void progress_update(progress_t *progress, int job_id, size_t processed)
{
    if (!progress || job_id < 0 || job_id >= progress->count)
        return;

    __atomic_store_n(&progress->slots[job_id].processed, processed,
                     __ATOMIC_RELAXED);
}

void progress_job_finish(progress_t *progress, int job_id)
{
    if (!progress || job_id < 0 || job_id >= progress->count)
        return;

    pthread_rwlock_wrlock(&progress->lock);
    __atomic_store_n(&progress->slots[job_id].processed,
                     progress->slots[job_id].total,
                     __ATOMIC_RELAXED);
    progress->slots[job_id].active = 0;
    progress->slots[job_id].done = 1;
    pthread_rwlock_unlock(&progress->lock);
}

void progress_request_stop(progress_t *progress)
{
    if (!progress)
        return;

    __atomic_store_n(&progress->stop, 1, __ATOMIC_RELAXED);
}

static void clear_previous_lines(int lines)
{
    for (int i = 0; i < lines; i++)
        fprintf(stderr, "\033[A\r\033[K");
}

static void draw_bar(size_t processed, size_t total)
{
    int percent = 0;
    int filled = 0;

    if (total > 0) {
        percent = (int)(((double)processed / (double)total) * 100.0);
        if (percent > 100)
            percent = 100;
        filled = (percent * PROGRESS_BAR_WIDTH) / 100;
    }

    for (int i = 0; i < PROGRESS_BAR_WIDTH; i++)
        fputs(i < filled ? "█" : "░", stderr);
    fprintf(stderr, " %d%% (%zu/%zu bytes)", percent, processed, total);
}

void *progress_monitor_run(void *arg)
{
    progress_t *progress = (progress_t *)arg;
    const struct timespec interval = {0, 100000000L};

    while (!__atomic_load_n(&progress->stop, __ATOMIC_RELAXED)) {
        int lines = 0;

        nanosleep(&interval, NULL);
        pthread_rwlock_rdlock(&progress->lock);
        clear_previous_lines(progress->lines_printed);

        for (int i = 0; i < progress->count; i++) {
            if (progress->slots[i].active) {
                size_t processed = __atomic_load_n(
                    &progress->slots[i].processed, __ATOMIC_RELAXED);
                size_t total = progress->slots[i].total;

                fprintf(stderr, "[job-%02d] ", i);
                draw_bar(processed, total);
                fputc('\n', stderr);
                lines++;
            }
        }

        progress->lines_printed = lines;
        fflush(stderr);
        pthread_rwlock_unlock(&progress->lock);
    }

    pthread_rwlock_wrlock(&progress->lock);
    clear_previous_lines(progress->lines_printed);
    progress->lines_printed = 0;
    fflush(stderr);
    pthread_rwlock_unlock(&progress->lock);

    return NULL;
}

void progress_rle_callback(void *ctx, size_t processed)
{
    progress_update_ctx_t *update = (progress_update_ctx_t *)ctx;

    if (!update)
        return;

    progress_update(update->progress, update->job_id, processed);
}
