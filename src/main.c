#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "logger.h"
#include "threadpool.h"
#include "worker.h"

#define MAX_FILES 64
#define OUTPUT_PATH_MAX 1024

static volatile sig_atomic_t g_interrupted = 0;

static void handle_sigint(int signo)
{
    (void)signo;
    g_interrupted = 1;
}

static int install_sigint_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  %s [options] file1 file2 ...\n\n"
        "Options:\n"
        "  -d          Decompress (default: compress)\n"
        "  -j <N>      Max worker threads (default: min(files, 4))\n"
        "  -o <dir>    Write all outputs into directory\n"
        "  -s          Print compression statistics after all jobs finish\n"
        "  -h          Show this help\n\n"
        "Output files:\n"
        "  Compress:   file.rle\n"
        "  Decompress: file.out  (strips .rle extension)\n\n"
        "Notes:\n"
        "  All options must come before file arguments.\n",
        prog);
}

static int parse_positive_int(const char *value, int *out)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed < 1)
        return -1;
    if (parsed > MAX_FILES)
        parsed = MAX_FILES;

    *out = (int)parsed;
    return 0;
}

static int ensure_output_dir(const char *dir)
{
    struct stat st;

    if (!dir)
        return 0;

    if (stat(dir, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Error: output path '%s' exists but is not a directory\n",
                    dir);
            return -1;
        }
        return 0;
    }

    if (errno != ENOENT) {
        fprintf(stderr, "Error: cannot stat output directory '%s': %s\n",
                dir, strerror(errno));
        return -1;
    }

    if (mkdir(dir, S_IRWXU) != 0) {
        fprintf(stderr, "Error: cannot create output directory '%s': %s\n",
                dir, strerror(errno));
        return -1;
    }

    return 0;
}

static const char *path_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static const char *path_sep_for_dir(const char *dir)
{
    size_t len = strlen(dir);
    return (len > 0 && dir[len - 1] == '/') ? "" : "/";
}

/* Build output path:
 *   compress   -> append .rle
 *   decompress -> strip .rle suffix if present, append .out
 * If output_dir is set, write the basename into that directory. */
static int make_output_path(const char *input, op_mode_t mode,
                            const char *output_dir,
                            char *out, size_t out_len)
{
    const char *name = output_dir ? path_leaf(input) : input;
    size_t name_len = strlen(name);
    int needed;

    if (mode == MODE_COMPRESS) {
        if (output_dir) {
            needed = snprintf(out, out_len, "%s%s%s.rle",
                              output_dir, path_sep_for_dir(output_dir), name);
        } else {
            needed = snprintf(out, out_len, "%s.rle", name);
        }
    } else {
        if (name_len > 4 && strcmp(name + name_len - 4, ".rle") == 0) {
            if (output_dir) {
                needed = snprintf(out, out_len, "%s%s%.*s.out",
                                  output_dir, path_sep_for_dir(output_dir),
                                  (int)(name_len - 4), name);
            } else {
                needed = snprintf(out, out_len, "%.*s.out",
                                  (int)(name_len - 4), name);
            }
        } else {
            if (output_dir) {
                needed = snprintf(out, out_len, "%s%s%s.out",
                                  output_dir, path_sep_for_dir(output_dir), name);
            } else {
                needed = snprintf(out, out_len, "%s.out", name);
            }
        }
    }

    if (needed < 0 || (size_t)needed >= out_len) {
        fprintf(stderr, "Error: output path too long for '%s'\n", input);
        return -1;
    }

    return 0;
}

static size_t stats_original_bytes(const job_t *job)
{
    return (job->mode == MODE_COMPRESS) ? job->in_size : job->out_size;
}

static size_t stats_compressed_bytes(const job_t *job)
{
    return (job->mode == MODE_COMPRESS) ? job->out_size : job->in_size;
}

static void print_stats(const job_t *jobs, int n_files)
{
    long long total_saved = 0;
    double ratio_sum = 0.0;
    int ratio_count = 0;

    printf("\nFile | Original | Compressed | Ratio | Time(ms) | Status\n");
    printf("---- | -------- | ---------- | ----- | -------- | ------\n");

    for (int i = 0; i < n_files; i++) {
        size_t original = stats_original_bytes(&jobs[i]);
        size_t compressed = stats_compressed_bytes(&jobs[i]);
        double ratio = 0.0;

        if (original > 0)
            ratio = ((double)compressed / (double)original) * 100.0;

        printf("%s | %zu | %zu | %.2f%% | %.2f | %s\n",
               jobs[i].input_path,
               original,
               compressed,
               ratio,
               jobs[i].elapsed_ms,
               jobs[i].status ? jobs[i].status : "PENDING");

        if (jobs[i].result == 0 && original > 0) {
            total_saved += (long long)original - (long long)compressed;
            ratio_sum += ratio;
            ratio_count++;
        }
    }

    printf("total bytes saved | %lld\n", total_saved);
    printf("average ratio | %.2f%%\n",
           ratio_count > 0 ? ratio_sum / (double)ratio_count : 0.0);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    op_mode_t mode = MODE_COMPRESS;
    int max_jobs = 0;
    int stats_enabled = 0;
    const char *output_dir = NULL;
    int i = 1;

    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;

        } else if (strcmp(argv[i], "-d") == 0) {
            mode = MODE_DECOMPRESS;
            i++;

        } else if (strcmp(argv[i], "-s") == 0) {
            stats_enabled = 1;
            i++;

        } else if (strcmp(argv[i], "-j") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -j requires a number\n");
                return 1;
            }
            if (parse_positive_int(argv[i + 1], &max_jobs) != 0) {
                fprintf(stderr, "Error: -j must be >= 1\n");
                return 1;
            }
            i += 2;

        } else if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires a directory\n");
                return 1;
            }
            output_dir = argv[i + 1];
            i += 2;

        } else {
            fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    int file_start = i;
    int n_files = argc - file_start;

    if (n_files <= 0) {
        fprintf(stderr, "Error: no input files given\n");
        usage(argv[0]);
        return 1;
    }
    if (n_files > MAX_FILES) {
        fprintf(stderr, "Error: max %d files at once\n", MAX_FILES);
        return 1;
    }
    if (max_jobs == 0)
        max_jobs = (n_files < 4) ? n_files : 4;
    if (max_jobs > n_files)
        max_jobs = n_files;

    for (int j = file_start; j < argc; j++) {
        if (argv[j][0] == '-') {
            fprintf(stderr,
                "Warning: '%s' looks like a flag but comes after files.\n"
                "         All options must come before file arguments.\n",
                argv[j]);
        }
    }

    if (ensure_output_dir(output_dir) != 0)
        return 1;

    job_t jobs[MAX_FILES];
    char out_paths[MAX_FILES][OUTPUT_PATH_MAX];

    for (int j = 0; j < n_files; j++) {
        jobs[j].input_path = argv[file_start + j];
        jobs[j].mode = mode;
        jobs[j].job_id = j;
        jobs[j].result = -1;
        jobs[j].elapsed_ms = 0.0;
        jobs[j].in_size = 0;
        jobs[j].out_size = 0;
        jobs[j].status = "PENDING";

        if (make_output_path(jobs[j].input_path, mode, output_dir,
                             out_paths[j], sizeof(out_paths[j])) != 0)
            return 1;
        jobs[j].output_path = out_paths[j];
    }

    if (install_sigint_handler() != 0)
        return 1;

    logger_init();

    struct timespec wall_start, wall_end;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);

    printf("MultiSync  |  mode: %s  |  files: %d  |  worker threads: %d\n",
           mode == MODE_COMPRESS ? "compress" : "decompress",
           n_files,
           max_jobs);
    printf("-----------------------------------------------------\n");

    threadpool_t pool;
    if (threadpool_init(&pool, max_jobs, max_jobs) != 0) {
        fprintf(stderr, "Error: could not start thread pool\n");
        logger_destroy();
        return 1;
    }

    int submitted = 0;
    int dispatch_error = 0;
    for (int j = 0; j < n_files; j++) {
        if (g_interrupted)
            break;

        int rc = threadpool_submit(&pool, &jobs[j], &g_interrupted);
        if (rc == 1)
            break;
        if (rc != 0) {
            fprintf(stderr, "Error: failed to enqueue job %d\n", j);
            dispatch_error = 1;
            break;
        }
        submitted++;
    }

    int cancelled = n_files - submitted;
    if (!g_interrupted)
        cancelled = 0;
    for (int j = submitted; j < n_files; j++) {
        if (g_interrupted) {
            jobs[j].status = "CANCELLED";
        } else if (dispatch_error) {
            jobs[j].status = "FAIL";
        }
    }

    threadpool_shutdown(&pool);

    clock_gettime(CLOCK_MONOTONIC, &wall_end);
    double wall_ms = (wall_end.tv_sec - wall_start.tv_sec) * 1000.0
                   + (wall_end.tv_nsec - wall_start.tv_nsec) / 1e6;

    printf("-----------------------------------------------------\n");
    int ok = 0;
    int fail = 0;
    for (int j = 0; j < n_files; j++)
        jobs[j].result == 0 ? ok++ : fail++;

    printf("Done: %d succeeded, %d failed  |  wall time: %.2f ms\n",
           ok, fail, wall_ms);

    if (g_interrupted)
        printf("Interrupted — %d jobs cancelled\n", cancelled);

    if (stats_enabled)
        print_stats(jobs, n_files);

    logger_destroy();

    if (g_interrupted)
        return 130;
    return (fail > 0 || dispatch_error) ? 1 : 0;
}
