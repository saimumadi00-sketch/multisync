#include "logger.h"
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static pthread_mutex_t log_mutex;
static int log_silent;

void logger_init(void) {
    pthread_mutex_init(&log_mutex, NULL);
    log_silent = 0;
}

void logger_destroy(void) {
    pthread_mutex_destroy(&log_mutex);
}

void logger_set_silent(int silent) {
    log_silent = silent;
}

void logger_log(int job_id, const char *fmt, ...)
{
    if (log_silent)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* Show elapsed ms since program epoch (mod 100000 keeps it short) */
    long ms = (ts.tv_sec * 1000L) + (ts.tv_nsec / 1000000L);

    /* Build the message into a local buffer first so we hold the
     * mutex for the shortest possible time (just the write+flush). */
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);   /* done with va_list — release before taking mutex */

    pthread_mutex_lock(&log_mutex);
    printf("[job-%02d | %ldms] %s\n", job_id, ms % 100000L, msg);
    fflush(stdout);
    pthread_mutex_unlock(&log_mutex);
}
