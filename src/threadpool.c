#include "threadpool.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void *threadpool_worker(void *arg)
{
    threadpool_t *pool = (threadpool_t *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);
        while (pool->count == 0 && !pool->shutting_down)
            pthread_cond_wait(&pool->not_empty, &pool->mutex);

        if (pool->count == 0 && pool->shutting_down) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        job_t *job = pool->queue[pool->head];
        pool->head = (pool->head + 1) % pool->capacity;
        pool->count--;
        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        worker_run(job);
    }

    return NULL;
}

static void add_wait_time(struct timespec *ts, long milliseconds)
{
    ts->tv_nsec += (milliseconds % 1000L) * 1000000L;
    ts->tv_sec += milliseconds / 1000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

int threadpool_init(threadpool_t *pool, int thread_count, int queue_capacity)
{
    if (!pool || thread_count < 1 || queue_capacity < 1)
        return -1;

    memset(pool, 0, sizeof(*pool));
    pool->thread_count = thread_count;
    pool->capacity = queue_capacity;

    pool->threads = calloc((size_t)thread_count, sizeof(*pool->threads));
    pool->queue = calloc((size_t)queue_capacity, sizeof(*pool->queue));
    if (!pool->threads || !pool->queue)
        goto fail_alloc;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0)
        goto fail_alloc;
    if (pthread_cond_init(&pool->not_full, NULL) != 0)
        goto fail_mutex;
    if (pthread_cond_init(&pool->not_empty, NULL) != 0)
        goto fail_not_full;

    for (int i = 0; i < thread_count; i++) {
        int rc = pthread_create(&pool->threads[i], NULL,
                                threadpool_worker, pool);
        if (rc != 0) {
            pthread_mutex_lock(&pool->mutex);
            pool->shutting_down = 1;
            pthread_cond_broadcast(&pool->not_empty);
            pthread_mutex_unlock(&pool->mutex);

            for (int j = 0; j < i; j++)
                pthread_join(pool->threads[j], NULL);

            pthread_cond_destroy(&pool->not_empty);
            goto fail_not_full;
        }
    }

    return 0;

fail_not_full:
    pthread_cond_destroy(&pool->not_full);
fail_mutex:
    pthread_mutex_destroy(&pool->mutex);
fail_alloc:
    free(pool->queue);
    free(pool->threads);
    memset(pool, 0, sizeof(*pool));
    return -1;
}

int threadpool_submit(threadpool_t *pool, job_t *job,
                      volatile sig_atomic_t *interrupted)
{
    if (!pool || !job)
        return -1;

    pthread_mutex_lock(&pool->mutex);
    while (pool->count == pool->capacity && !pool->shutting_down) {
        if (interrupted && *interrupted) {
            pthread_mutex_unlock(&pool->mutex);
            return 1;
        }

        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        add_wait_time(&deadline, 100L);
        int rc = pthread_cond_timedwait(&pool->not_full,
                                        &pool->mutex,
                                        &deadline);
        if (rc != 0 && rc != ETIMEDOUT) {
            pthread_mutex_unlock(&pool->mutex);
            return -1;
        }
    }

    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }
    if (interrupted && *interrupted) {
        pthread_mutex_unlock(&pool->mutex);
        return 1;
    }

    pool->queue[pool->tail] = job;
    pool->tail = (pool->tail + 1) % pool->capacity;
    pool->count++;
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void threadpool_shutdown(threadpool_t *pool)
{
    if (!pool || !pool->threads)
        return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->thread_count; i++)
        pthread_join(pool->threads[i], NULL);

    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_mutex_destroy(&pool->mutex);
    free(pool->queue);
    free(pool->threads);
    memset(pool, 0, sizeof(*pool));
}
