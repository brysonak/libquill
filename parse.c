#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <threads.h>
#include <windows.h>
typedef thrd_t thread;
#define WORKER(name) static int name(void *arg)
#define WORKER_END return 0
#define SPAWN(t, fn, arg) (thrd_create(&(t), fn, arg) == thrd_success)
#define JOIN(t) thrd_join(t, NULL)
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t thread;
#define WORKER(name) static void *name(void *arg)
#define WORKER_END return NULL
#define SPAWN(t, fn, arg) (pthread_create(&(t), NULL, fn, arg) == 0)
#define JOIN(t) pthread_join(t, NULL)
#endif

#define MAX_THREADS 256

typedef struct {
    uint64_t row, col;
} match;

typedef struct {
    const char *buf, *name;
    size_t len, name_len, k;
    int name_quote;
    quill_range *ranges;
    uint64_t rows, fields;
    match *hits;
    size_t nhits, cap;
    int failed;
} job;

static double now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int cores(void)
{
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
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

WORKER(count_job)
{
    job *j = (job *)arg;
    quill_count(j->buf, &j->ranges[j->k]);
    WORKER_END;
}

/* Unquoted fields and quoted ones without inner quotes are their own value, so
   only a same length byte compare is needed. Quoted fields with inner quotes are
   unescaped first, unescaping only shrinks them, hence the length check.
   Counters live in locals until the end, jobs sit side by side in memory and per
   field stores to them would make the threads fight over cache lines */
WORKER(search_job)
{
    job *j = (job *)arg;
    quill_parser *p = (quill_parser *)malloc(sizeof *p);
    char *tmp = NULL;
    size_t cap = 0;
    uint64_t rows = 0, fields = 0, col = 0;
    quill_field f;
    if (!p || quill_init_range(p, ',', 0, j->buf, j->len, j->ranges, j->k)) {
        j->failed = 1;
        free(p);
        WORKER_END;
    }
    while (quill_next(p, &f) == QUILL_FIELD) {
        int hit = 0;
        fields++;
        col++;
        if (f.len == j->name_len) {
            hit = !j->name_quote && !memcmp(f.ptr, j->name, f.len);
        } else if (f.quoted && f.len > j->name_len && memchr(f.ptr, '"', f.len)) {
            size_t n;
            const char *v;
            if (f.len > cap) {
                char *t = (char *)realloc(tmp, cap = f.len * 2);
                if (!t) {
                    j->failed = 1;
                    break;
                }
                tmp = t;
            }
            v = quill_value(&f, tmp, &n);
            hit = n == j->name_len && !memcmp(v, j->name, n);
        }
        if (hit) {
            if (j->nhits == j->cap) {
                match *t = (match *)realloc(j->hits, (j->cap = j->cap ? j->cap * 2 : 64) * sizeof *t);
                if (!t) {
                    j->failed = 1;
                    break;
                }
                j->hits = t;
            }
            j->hits[j->nhits].row = rows + 1;
            j->hits[j->nhits++].col = col;
        }
        if (f.eol) {
            rows++;
            col = 0;
        }
    }
    j->rows = rows;
    j->fields = fields;
    free(tmp);
    free(p);
    WORKER_END;
}

typedef struct {
    double count, search;
    uint64_t rows, fields, hits;
} result;

/* Returns 0 on failure, with print set it lists the first matches by global row.
   quill_init_range only reads the ranges before k, so the last range is never counted */
static int run(const char *buf, size_t len, const char *name, size_t n, int print, result *res)
{
    static quill_range ranges[MAX_THREADS];
    static job jobs[MAX_THREADS];
    thread t[MAX_THREADS];
    size_t k, started, shown = 0;
    uint64_t row_base = 0;
    double t0, t1, t2;
    for (k = 0; k < n; k++) {
        ranges[k].start = len / n * k;
        ranges[k].end = k + 1 == n ? len : len / n * (k + 1);
        memset(&jobs[k], 0, sizeof jobs[k]);
        jobs[k].buf = buf;
        jobs[k].len = len;
        jobs[k].name = name;
        jobs[k].name_len = strlen(name);
        jobs[k].name_quote = strchr(name, '"') != NULL;
        jobs[k].k = k;
        jobs[k].ranges = ranges;
    }
    t0 = now();
    for (started = 0; started + 1 < n && SPAWN(t[started], count_job, &jobs[started]); started++) {
    }
    for (k = 0; k < started; k++)
        JOIN(t[k]);
    if (started + 1 < n)
        return 0;
    t1 = now();
    for (started = 0; started < n && SPAWN(t[started], search_job, &jobs[started]); started++) {
    }
    for (k = 0; k < started; k++)
        JOIN(t[k]);
    t2 = now();
    if (started < n)
        return 0;
    memset(res, 0, sizeof *res);
    res->count = t1 - t0;
    res->search = t2 - t1;
    for (k = 0; k < n; k++)
        if (jobs[k].failed)
            return 0;
    for (k = 0; k < n; k++) {
        size_t h;
        for (h = 0; print && h < jobs[k].nhits && shown < 10; h++, shown++)
            printf("  row %llu, column %llu\n", (unsigned long long)(row_base + jobs[k].hits[h].row),
                   (unsigned long long)jobs[k].hits[h].col);
        row_base += jobs[k].rows;
        res->rows += jobs[k].rows;
        res->fields += jobs[k].fields;
        res->hits += jobs[k].nhits;
        free(jobs[k].hits);
    }
    if (print && res->hits > shown)
        printf("  ... and %llu more\n", (unsigned long long)(res->hits - shown));
    return 1;
}

static void report(const char *label, size_t len, const result *r)
{
    double total = r->count + r->search;
    printf("%-10s %8.1f ms  (count %6.1f ms, search %7.1f ms)  %6.2f GB/s\n", label, total * 1e3, r->count * 1e3,
           r->search * 1e3, (double)len / total / 1e9);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "big.csv", *name = argc > 2 ? argv[2] : "Debbie";
    int threads = argc > 3 ? atoi(argv[3]) : cores();
    result one, many;
    size_t len;
    double t0 = now(), load_time;
    char *buf = load(path, &len);
    load_time = now() - t0;
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    if (threads < 1 || threads > MAX_THREADS) {
        fprintf(stderr, "threads must be 1 to %d\n", MAX_THREADS);
        return 1;
    }
    printf("%s: %.1f MB, loaded in %.1f ms\n", path, (double)len / 1e6, load_time * 1e3);
    printf("fields equal to \"%s\":\n", name);
    if (!run(buf, len, name, (size_t)threads, 1, &many) || !run(buf, len, name, 1, 0, &one) ||
        !run(buf, len, name, (size_t)threads, 0, &many)) {
        fprintf(stderr, "out of memory or cannot start threads\n");
        return 1;
    }
    if (one.hits != many.hits || one.rows != many.rows) {
        fprintf(stderr, "1 thread and %d threads disagree\n", threads);
        return 1;
    }
    printf("%llu matches in %llu rows, %llu fields\n\n", (unsigned long long)many.hits,
           (unsigned long long)many.rows, (unsigned long long)many.fields);
    report("1 thread", len, &one);
    {
        char label[32];
        sprintf(label, "%d threads", threads);
        report(label, len, &many);
    }
    printf("speedup    %8.2fx\n", (one.count + one.search) / (many.count + many.search));
    free(buf);
    return 0;
}
