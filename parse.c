#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <unistd.h>

#define MAX_THREADS 256

typedef struct {
    uint64_t row, col;
} match;

typedef struct {
    const char *buf, *name;
    size_t len, name_len, k;
    quill_range *ranges;
    uint64_t rows, fields;
    match *hits;
    size_t nhits, cap;
    int failed;
} job;

static int cores(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
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

static void *count_job(void *arg)
{
    job *j = (job *)arg;
    quill_count(j->buf, &j->ranges[j->k]);
    return NULL;
}

/* Unquoted fields and quoted ones without inner quotes are their own value, so
   only a same length byte compare is needed. Quoted fields with inner quotes are
   unescaped first, unescaping only shrinks them, hence the length check */
static void *search_job(void *arg)
{
    job *j = (job *)arg;
    quill_parser *p = (quill_parser *)malloc(sizeof *p);
    char *tmp = NULL;
    size_t cap = 0;
    uint64_t col = 0;
    quill_field f;
    if (!p || quill_init_range(p, ',', 0, j->buf, j->len, j->ranges, j->k)) {
        j->failed = 1;
        free(p);
        return NULL;
    }
    while (quill_next(p, &f) == QUILL_FIELD) {
        int hit = 0;
        j->fields++;
        col++;
        if (f.len == j->name_len) {
            hit = !memcmp(f.ptr, j->name, f.len);
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
            j->hits[j->nhits].row = j->rows + 1;
            j->hits[j->nhits++].col = col;
        }
        if (f.eol) {
            j->rows++;
            col = 0;
        }
    }
    free(tmp);
    free(p);
    return NULL;
}

/* Returns 0 on failure. quill_init_range only reads the ranges before k, so the
   last range is never counted */
static int run(const char *buf, size_t len, const char *name, size_t n)
{
    static quill_range ranges[MAX_THREADS];
    static job jobs[MAX_THREADS];
    pthread_t t[MAX_THREADS];
    size_t k, started, shown = 0;
    uint64_t row_base = 0, rows = 0, fields = 0, hits = 0;
    for (k = 0; k < n; k++) {
        ranges[k].start = len / n * k;
        ranges[k].end = k + 1 == n ? len : len / n * (k + 1);
        memset(&jobs[k], 0, sizeof jobs[k]);
        jobs[k].buf = buf;
        jobs[k].len = len;
        jobs[k].name = name;
        jobs[k].name_len = strlen(name);
        jobs[k].k = k;
        jobs[k].ranges = ranges;
    }
    for (started = 0; started + 1 < n && pthread_create(&t[started], NULL, count_job, &jobs[started]) == 0; started++) {
    }
    for (k = 0; k < started; k++)
        pthread_join(t[k], NULL);
    if (started + 1 < n)
        return 0;
    for (started = 0; started < n && pthread_create(&t[started], NULL, search_job, &jobs[started]) == 0; started++) {
    }
    for (k = 0; k < started; k++)
        pthread_join(t[k], NULL);
    if (started < n)
        return 0;
    for (k = 0; k < n; k++)
        if (jobs[k].failed)
            return 0;
    for (k = 0; k < n; k++) {
        size_t h;
        for (h = 0; h < jobs[k].nhits && shown < 10; h++, shown++)
            printf("  row %llu, column %llu\n", (unsigned long long)(row_base + jobs[k].hits[h].row),
                   (unsigned long long)jobs[k].hits[h].col);
        row_base += jobs[k].rows;
        rows += jobs[k].rows;
        fields += jobs[k].fields;
        hits += jobs[k].nhits;
        free(jobs[k].hits);
    }
    if (hits > shown)
        printf("  ... and %llu more\n", (unsigned long long)(hits - shown));
    printf("%llu matches in %llu rows, %llu fields\n", (unsigned long long)hits, (unsigned long long)rows,
           (unsigned long long)fields);
    return 1;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "big.csv", *name = argc > 2 ? argv[2] : "Debbie";
    int threads = argc > 3 ? atoi(argv[3]) : cores();
    size_t len;
    char *buf = load(path, &len);
    if (!buf) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    if (threads < 1 || threads > MAX_THREADS) {
        fprintf(stderr, "threads must be 1 to %d\n", MAX_THREADS);
        return 1;
    }
    printf("%s: %.1f MB, %d threads\n", path, (double)len / 1e6, threads);
    printf("fields equal to \"%s\":\n", name);
    if (!run(buf, len, name, (size_t)threads)) {
        fprintf(stderr, "out of memory or cannot start threads\n");
        return 1;
    }
    free(buf);
    return 0;
}
