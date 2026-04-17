#include "logger.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static pthread_mutex_t log_mutex;

void logger_init(void) {
    pthread_mutex_init(&log_mutex, NULL);
}

void logger_destroy(void) {
    pthread_mutex_destroy(&log_mutex);
}

void logger_log(int job_id, const char *fmt, ...) {
    /* Get timestamp */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long ms = ts.tv_nsec / 1000000;

    va_list args;
    va_start(args, fmt);

    pthread_mutex_lock(&log_mutex);

    printf("[job-%02d | %ld ms] ", job_id, ms % 100000);
    vprintf(fmt, args);
    printf("\n");
    fflush(stdout);

    pthread_mutex_unlock(&log_mutex);

    va_end(args);
}
