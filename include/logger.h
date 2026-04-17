#ifndef LOGGER_H
#define LOGGER_H

/* Initialise / destroy the mutex-protected logger */
void logger_init(void);
void logger_destroy(void);

/* Thread-safe log line. Prepends [job_id] to the message. */
void logger_log(int job_id, const char *fmt, ...);

#endif /* LOGGER_H */
