#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "worker.h"
#include "logger.h"

#define MAX_FILES 64

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage:\n"
        "  %s [options] file1 file2 ...\n\n"
        "Options:\n"
        "  -d          Decompress (default: compress)\n"
        "  -j <N>      Max parallel threads (default: one per file)\n"
        "  -h          Show this help\n\n"
        "Output files:\n"
        "  Compress:   file.rle\n"
        "  Decompress: file.out  (strips .rle extension)\n\n"
        "Notes:\n"
        "  All options must come before file arguments.\n",
        prog);
}

/* Build output path:
 *   compress   -> append .rle
 *   decompress -> strip .rle suffix if present, append .out */
static void make_output_path(const char *input, op_mode_t mode,
                              char *out, size_t out_len)
{
    if (mode == MODE_COMPRESS) {
        snprintf(out, out_len, "%s.rle", input);
    } else {
        size_t in_len = strlen(input);
        if (in_len > 4 && strcmp(input + in_len - 4, ".rle") == 0)
            snprintf(out, out_len, "%.*s.out", (int)(in_len - 4), input);
        else
            snprintf(out, out_len, "%s.out", input);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    op_mode_t mode     = MODE_COMPRESS;
    int       max_jobs = MAX_FILES;
    int       i        = 1;

    /* ── Parse flags (must come before file arguments) ── */
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]); return 0;

        } else if (strcmp(argv[i], "-d") == 0) {
            mode = MODE_DECOMPRESS;
            i++;

        } else if (strcmp(argv[i], "-j") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -j requires a number\n");
                return 1;
            }
            max_jobs = atoi(argv[++i]);
            if (max_jobs < 1) {
                fprintf(stderr, "Error: -j must be >= 1\n");
                return 1;
            }
            i++;

        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    /* Everything after flags is a file */
    int file_start = i;
    int n_files    = argc - file_start;

    if (n_files <= 0) {
        fprintf(stderr, "Error: no input files given\n");
        usage(argv[0]);
        return 1;
    }
    if (n_files > MAX_FILES) {
        fprintf(stderr, "Error: max %d files at once\n", MAX_FILES);
        return 1;
    }

    /* Warn if we see what looks like a flag after files */
    for (int j = file_start; j < argc; j++) {
        if (argv[j][0] == '-') {
            fprintf(stderr,
                "Warning: '%s' looks like a flag but comes after files.\n"
                "         All options must come before file arguments.\n",
                argv[j]);
        }
    }

    /* ── Build job list ── */
    job_t     jobs[MAX_FILES];
    char      out_paths[MAX_FILES][512];
    pthread_t threads[MAX_FILES];

    for (int j = 0; j < n_files; j++) {
        jobs[j].input_path  = argv[file_start + j];
        jobs[j].mode        = mode;
        jobs[j].job_id      = j;
        jobs[j].result      = -1;
        jobs[j].elapsed_ms  = 0.0;
        make_output_path(jobs[j].input_path, mode,
                         out_paths[j], sizeof(out_paths[j]));
        jobs[j].output_path = out_paths[j];
    }

    /* ── Run ── */
    logger_init();

    struct timespec wall_start, wall_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);

    printf("MultiSync  |  mode: %s  |  files: %d  |  max threads: %d\n",
           mode == MODE_COMPRESS ? "compress" : "decompress",
           n_files,
           max_jobs < n_files ? max_jobs : n_files);
    printf("─────────────────────────────────────────────────────\n");

    int spawned = 0;
    while (spawned < n_files) {
        int batch = (max_jobs < n_files - spawned) ? max_jobs : (n_files - spawned);

        for (int j = 0; j < batch; j++) {
            int rc = pthread_create(&threads[spawned + j], NULL,
                                    worker_run, &jobs[spawned + j]);
            if (rc != 0) {
                fprintf(stderr, "Error: pthread_create failed for job %d (rc=%d)\n",
                        spawned + j, rc);
                /* Join only what was actually spawned */
                for (int k = 0; k < j; k++)
                    pthread_join(threads[spawned + k], NULL);
                logger_destroy();
                return 1;
            }
        }

        for (int j = 0; j < batch; j++)
            pthread_join(threads[spawned + j], NULL);

        spawned += batch;
    }

    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    double wall_ms = (wall_end.tv_sec  - wall_start.tv_sec)  * 1000.0
                   + (wall_end.tv_nsec - wall_start.tv_nsec) / 1e6;

    /* ── Summary ── */
    printf("─────────────────────────────────────────────────────\n");
    int ok = 0, fail = 0;
    for (int j = 0; j < n_files; j++)
        jobs[j].result == 0 ? ok++ : fail++;

    printf("Done: %d succeeded, %d failed  |  wall time: %.2f ms\n",
           ok, fail, wall_ms);

    logger_destroy();
    return (fail > 0) ? 1 : 0;
}
