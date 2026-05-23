#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <threads.h>
typedef thrd_t thread;
#define WORKER(name)      static int name(void *arg)
#define WORKER_END        return 0
#define SPAWN(t, fn, arg) (thrd_create(&(t), fn, arg) == thrd_success)
#define JOIN(t)           thrd_join(t, NULL)
#else
#include <pthread.h>
typedef pthread_t thread;
#define WORKER(name)      static void *name(void *arg)
#define WORKER_END        return NULL
#define SPAWN(t, fn, arg) (pthread_create(&(t), NULL, fn, arg) == 0)
#define JOIN(t)           pthread_join(t, NULL)
#endif

#define MAX_THREADS 16

static char delim = ',';

typedef struct {
    const char *buf;
    size_t len, k, fields, bytes;
    quill_range *ranges;
    int guess;
} job;

static double now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

WORKER(count_job)
{
    job *j = (job *)arg;
    quill_count(j->buf, &j->ranges[j->k]);
    WORKER_END;
}

WORKER(parse_job)
{
    job *j = (job *)arg;
    quill_parser *p = (quill_parser *)malloc(sizeof *p);
    char *tmp = NULL;
    size_t cap = 0, fields = 0, bytes = 0;
    quill_field f;
    int rc = !p       ? -1
           : j->guess ? quill_init_guess(p, delim, 0, j->buf, j->len, j->ranges, j->k)
                      : quill_init_range(p, delim, 0, j->buf, j->len, j->ranges, j->k);
    if (p && rc == 0) {
        while (quill_next(p, &f) == QUILL_FIELD) {
            size_t n;
            if (f.len > cap) {
                char *t = (char *)realloc(tmp, f.len * 2);
                if (!t)
                    break;
                tmp = t;
                cap = f.len * 2;
            }
            quill_value(&f, tmp, &n);
            fields++;
            bytes += n;
        }
    }
    j->fields = fields;
    j->bytes = bytes;
    free(tmp);
    free(p);
    WORKER_END;
}

static int parse_threaded(const char *buf, size_t len, size_t n, int guess, size_t *fields, size_t *bytes)
{
    quill_range r[MAX_THREADS];
    job jobs[MAX_THREADS];
    thread t[MAX_THREADS];
    size_t k, started;
    for (k = 0; k < n; k++) {
        r[k].start = len / n * k;
        r[k].end = k + 1 == n ? len : len / n * (k + 1);
        jobs[k].buf = buf;
        jobs[k].len = len;
        jobs[k].k = k;
        jobs[k].ranges = r;
        jobs[k].guess = guess;
    }
    if (!guess) {
        for (started = 0; started + 1 < n && SPAWN(t[started], count_job, &jobs[started]); started++) {
        }
        for (k = 0; k < started; k++)
            JOIN(t[k]);
        if (started + 1 < n)
            return 0;
    }
    for (started = 0; started < n && SPAWN(t[started], parse_job, &jobs[started]); started++) {
    }
    for (k = 0; k < started; k++)
        JOIN(t[k]);
    *fields = *bytes = 0;
    for (k = 0; k < started; k++) {
        if (guess && !quill_guess_ok(r, k)) {
            jobs[k].guess = 0;
            parse_job(&jobs[k]);
        }
        *fields += jobs[k].fields;
        *bytes += jobs[k].bytes;
    }
    return started == n;
}

static char *load(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    size_t cap = 1 << 20, n = 0;
    char *s = NULL, *t;
    if (!f)
        return NULL;
    for (;;) {
        if (!(t = (char *)realloc(s, cap))) {
            free(s);
            s = NULL;
            break;
        }
        s = t;
        n += fread(s + n, 1, cap - n, f);
        if (n < cap) {
            if (ferror(f)) {
                free(s);
                s = NULL;
            }
            break;
        }
        cap *= 2;
    }
    fclose(f);
    *len = n;
    return s;
}

int main(int argc, char **argv)
{
    static quill_parser p;
    const char *path = NULL;
    double secs = 0.3;
    size_t len, k, cap = 0;
    char *data, *tmp = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)
            delim = !strcmp(argv[++i], "tab") ? '\t' : argv[i][0];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            secs = atof(argv[++i]);
        else
            path = argv[i];
    }
    if (!path) {
        fputs("usage: bench FILE [-d DELIM|tab] [-t SECONDS]\n", stderr);
        return 2;
    }
    if (!(data = load(path, &len))) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    printf("size %zu\n", len);
    for (k = 0; k <= (size_t)quill_i_best(); k++) {
        int strict;
        for (strict = 0; strict < 2; strict++) {
            size_t fields = 0, iters = 0;
            int rc = QUILL_END;
            double t0 = now(), t;
            do {
                quill_field f;
                quill_init(&p, delim, strict ? QUILL_STRICT : 0);
                p.run = quill_i_kernels[k].run;
                quill_feed(&p, data, len, 1);
                fields = 0;
                while ((rc = quill_next(&p, &f)) == QUILL_FIELD) {
                    size_t n;
                    if (f.len > cap) {
                        char *t2 = (char *)realloc(tmp, f.len * 2);
                        if (!t2)
                            return 1;
                        tmp = t2;
                        cap = f.len * 2;
                    }
                    quill_value(&f, tmp, &n);
                    fields++;
                }
                iters++;
            } while ((t = now() - t0) < secs);
            printf("kernel %s %s %.4f GB/s %.1f us %zu fields%s%s\n", quill_i_kernels[k].name,
                   strict ? "strict" : "lenient", (double)len * (double)iters / t / 1e9, t / (double)iters * 1e6,
                   fields, rc == QUILL_ERROR ? " error: " : "", rc == QUILL_ERROR ? p.err_msg : "");
        }
    }
    for (k = 1; len >= (1 << 20) && k <= MAX_THREADS; k *= 2) {
        int guess;
        for (guess = 1; guess >= 0; guess--) {
            size_t fields = 0, bytes = 0, iters = 0;
            double t0 = now(), t;
            do {
                if (!parse_threaded(data, len, k, guess, &fields, &bytes)) {
                    fprintf(stderr, "cannot start %zu threads\n", k);
                    return 1;
                }
                iters++;
            } while ((t = now() - t0) < secs);
            printf("%s %zu %.4f GB/s %zu fields\n", guess ? "threads" : "exact", k,
                   (double)len * (double)iters / t / 1e9, fields);
        }
    }
    free(data);
    free(tmp);
    return 0;
}
