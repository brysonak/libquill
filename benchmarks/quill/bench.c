#ifndef _WIN32
#define _GNU_SOURCE
#endif
#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <threads.h>
#include <windows.h>
typedef thrd_t thread;
#define WORKER(name)      static int name(void *arg)
#define WORKER_END        return 0
#define SPAWN(t, fn, arg) (thrd_create(&(t), fn, arg) == thrd_success)
#define JOIN(t)           thrd_join(t, NULL)
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
typedef pthread_t thread;
#define WORKER(name)      static void *name(void *arg)
#define WORKER_END        return NULL
#define SPAWN(t, fn, arg) (pthread_create(&(t), NULL, fn, arg) == 0)
#define JOIN(t)           pthread_join(t, NULL)
#endif

#define MAX_THREADS 256

enum { COUNT, SCAN, FIND };

typedef struct {
    const char *buf, *name;
    size_t len, name_len, k;
    quill_range *ranges;
    int action, exact;
    unsigned long long rows, fields, bytes, matches;
} job;

static const char *map_file(const char *path, size_t *len)
{
#ifdef _WIN32
    LARGE_INTEGER size;
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL), m;
    if (f == INVALID_HANDLE_VALUE || !GetFileSizeEx(f, &size))
        return NULL;
    *len = (size_t)size.QuadPart;
    if (!*len)
        return "";
    m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
    return m ? (const char *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : NULL;
#else
    struct stat st;
    int fd = open(path, O_RDONLY);
    void *m;
    if (fd < 0 || fstat(fd, &st))
        return NULL;
    *len = (size_t)st.st_size;
    if (!*len)
        return "";
#ifdef MAP_POPULATE
    m = mmap(NULL, *len, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
#else
    m = mmap(NULL, *len, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
    close(fd);
    return m == MAP_FAILED ? NULL : (const char *)m;
#endif
}

WORKER(parse_range)
{
    job *j = (job *)arg;
    quill_parser *p = (quill_parser *)malloc(sizeof *p);
    char *tmp = NULL;
    size_t cap = 0;
    quill_field f;
    unsigned long long rows = 0, fields = 0, bytes = 0, matches = 0;
    if (!p)
        WORKER_END;
    if (j->ranges) {
        if (j->exact)
            quill_init_range(p, ',', 0, j->buf, j->len, j->ranges, j->k);
        else
            quill_init_guess(p, ',', 0, j->buf, j->len, j->ranges, j->k);
    } else {
        quill_init(p, ',', 0);
        quill_feed(p, j->buf, j->len, 1);
    }
    while (quill_next(p, &f) == QUILL_FIELD) {
        rows += (unsigned)f.eol;
        if (j->action == COUNT)
            continue;
        fields++;
        if (j->action == FIND && f.len < j->name_len)
            continue;
        if (j->action == FIND && f.len == j->name_len && !f.quoted) {
            matches += !memcmp(f.ptr, j->name, f.len);
            continue;
        }
        {
            size_t n;
            const char *v;
            if (f.len > cap) {
                char *t = (char *)realloc(tmp, cap = f.len * 2);
                if (!t)
                    break;
                tmp = t;
            }
            v = quill_value(&f, tmp, &n);
            bytes += n;
            matches += j->action == FIND && n == j->name_len && !memcmp(v, j->name, n);
        }
    }
    j->rows = rows;
    j->fields = fields;
    j->bytes = bytes;
    j->matches = matches;
    free(tmp);
    free(p);
    WORKER_END;
}

int main(int argc, char **argv)
{
    static job jobs[MAX_THREADS];
    static quill_range ranges[MAX_THREADS];
    thread t[MAX_THREADS];
    int threads = 1, a = 1, action;
    size_t len, k, n;
    const char *buf, *name = "";
    unsigned long long rows = 0, fields = 0, bytes = 0, matches = 0;
    if (argc > 2 && !strcmp(argv[1], "-t")) {
        threads = atoi(argv[2]);
        threads = threads < 1 ? 1 : threads > MAX_THREADS ? MAX_THREADS : threads;
        a = 3;
    }
    action = !strcmp(argv[a], "count") ? COUNT : !strcmp(argv[a], "scan") ? SCAN : FIND;
    if (action == FIND)
        name = argc > a + 2 ? argv[a + 2] : "";
    if (!(buf = map_file(argv[a + 1], &len)))
        return 1;
    n = (size_t)threads;
    for (k = 0; k < n; k++) {
        jobs[k].buf = buf;
        jobs[k].len = len;
        jobs[k].name = name;
        jobs[k].name_len = strlen(name);
        jobs[k].action = action;
        jobs[k].k = k;
        jobs[k].ranges = n > 1 ? ranges : NULL;
        ranges[k].start = len / n * k;
        ranges[k].end = k + 1 == n ? len : len / n * (k + 1);
    }
    if (n == 1) {
        parse_range(&jobs[0]);
    } else {
        for (k = 0; k < n; k++)
            if (!SPAWN(t[k], parse_range, &jobs[k]))
                return 1;
        for (k = 0; k < n; k++)
            JOIN(t[k]);
        for (k = 0; k < n; k++) {
            if (!quill_guess_ok(ranges, k)) {
                jobs[k].exact = 1;
                parse_range(&jobs[k]);
            }
        }
    }
    for (k = 0; k < n; k++) {
        rows += jobs[k].rows;
        fields += jobs[k].fields;
        bytes += jobs[k].bytes;
        matches += jobs[k].matches;
    }
    if (action == COUNT)
        printf("rows %llu\n", rows);
    else if (action == SCAN)
        printf("rows %llu fields %llu bytes %llu\n", rows, fields, bytes);
    else
        printf("matches %llu\n", matches);
    return 0;
}
