#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "worker.h"

#include <pthread.h>
#include <signal.h>

typedef struct {
    pthread_t *threads;
    job_t    **queue;
    int        thread_count;
    int        capacity;
    int        head;
    int        tail;
    int        count;
    int        shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t  not_full;
    pthread_cond_t  not_empty;
} threadpool_t;

int  threadpool_init(threadpool_t *pool, int thread_count, int queue_capacity);
int  threadpool_submit(threadpool_t *pool, job_t *job,
                       volatile sig_atomic_t *interrupted);
void threadpool_shutdown(threadpool_t *pool);

#endif /* THREADPOOL_H */
