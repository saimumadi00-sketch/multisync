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
        "  Decompress: file.out  (strips .rle extension)\n",
        prog);
}

/* Build output path:
 *   compress   -> append .rle
 *   decompress -> strip .rle, append .out */
static void make_output_path(const char *input, op_mode_t mode,
                              char *out, size_t out_len)
{
    if (mode == MODE_COMPRESS) {
        snprintf(out, out_len, "%s.rle", input);
    } else {
        size_t in_len = strlen(input);
        if (in_len > 4 && strcmp(input + in_len - 4, ".rle") == 0) {
            snprintf(out, out_len, "%.*s.out", (int)(in_len - 4), input);
        } else {
            snprintf(out, out_len, "%s.out", input);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    op_mode_t mode     = MODE_COMPRESS;
    int       max_jobs = MAX_FILES;
    int       file_start = 1;

    /* ── Parse flags ── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) { usage(argv[0]); return 0; }
        if (strcmp(argv[i], "-d") == 0) { mode = MODE_DECOMPRESS; file_start = i + 1; }
        else if (strcmp(argv[i], "-j") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "Error: -j requires a number\n"); return 1; }
            max_jobs = atoi(argv[++i]);
            if (max_jobs < 1) { fprintf(stderr, "Error: -j must be >= 1\n"); return 1; }
            file_start = i + 1;
        }
    }

    int n_files = argc - file_start;
    if (n_files <= 0) { fprintf(stderr, "Error: no input files given\n"); usage(argv[0]); return 1; }
    if (n_files > MAX_FILES) { fprintf(stderr, "Error: max %d files at once\n", MAX_FILES); return 1; }

    /* ── Build job list ── */
    job_t      jobs[MAX_FILES];
    char       out_paths[MAX_FILES][512];
    pthread_t  threads[MAX_FILES];

    for (int i = 0; i < n_files; i++) {
        jobs[i].input_path  = argv[file_start + i];
        jobs[i].mode        = mode;
        jobs[i].job_id      = i;
        jobs[i].result      = -1;
        jobs[i].elapsed_ms  = 0.0;
        make_output_path(jobs[i].input_path, mode, out_paths[i], sizeof(out_paths[i]));
        jobs[i].output_path = out_paths[i];
    }

    /* ── Run ── */
    logger_init();

    struct timespec wall_start, wall_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);

    printf("MultiSync  |  mode: %s  |  files: %d  |  max threads: %d\n",
           mode == MODE_COMPRESS ? "compress" : "decompress",
           n_files, (max_jobs < n_files ? max_jobs : n_files));
    printf("─────────────────────────────────────────────────────\n");

    int spawned = 0;
    while (spawned < n_files) {
        /* Determine batch size (respects -j limit) */
        int batch = max_jobs < (n_files - spawned) ? max_jobs : (n_files - spawned);

        for (int i = 0; i < batch; i++)
            pthread_create(&threads[spawned + i], NULL, worker_run, &jobs[spawned + i]);

        for (int i = 0; i < batch; i++)
            pthread_join(threads[spawned + i], NULL);

        spawned += batch;
    }

    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    double wall_ms = (wall_end.tv_sec  - wall_start.tv_sec)  * 1000.0
                   + (wall_end.tv_nsec - wall_start.tv_nsec) / 1e6;

    /* ── Summary ── */
    printf("─────────────────────────────────────────────────────\n");
    int ok = 0, fail = 0;
    for (int i = 0; i < n_files; i++)
        jobs[i].result == 0 ? ok++ : fail++;

    printf("Done: %d succeeded, %d failed  |  wall time: %.2f ms\n",
           ok, fail, wall_ms);

    logger_destroy();
    return (fail > 0) ? 1 : 0;
}
