#define _GNU_SOURCE
#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum { COUNT, SCAN, FIND };

typedef struct {
    const char *buf, *name;
    size_t len, name_len;
    int action;
    unsigned long long rows, fields, bytes, matches;
} job;

static const char *map_file(const char *path, size_t *len)
{
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
}

static void parse(job *j)
{
    quill_parser *p = (quill_parser *)malloc(sizeof *p);
    char *tmp = NULL;
    size_t cap = 0;
    quill_field f;
    unsigned long long rows = 0, fields = 0, bytes = 0, matches = 0;
    if (!p)
        return;
    quill_init(p, ',', 0);
    quill_feed(p, j->buf, j->len, 1);
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
}

int main(int argc, char **argv)
{
    job j;
    size_t len;
    const char *buf, *name = "";
    int action = !strcmp(argv[1], "count") ? COUNT : !strcmp(argv[1], "scan") ? SCAN : FIND;
    if (action == FIND)
        name = argc > 3 ? argv[3] : "";
    if (!(buf = map_file(argv[2], &len)))
        return 1;
    memset(&j, 0, sizeof j);
    j.buf = buf;
    j.len = len;
    j.name = name;
    j.name_len = strlen(name);
    j.action = action;
    parse(&j);
    if (action == COUNT)
        printf("rows %llu\n", j.rows);
    else if (action == SCAN)
        printf("rows %llu fields %llu bytes %llu\n", j.rows, j.fields, j.bytes);
    else
        printf("matches %llu\n", j.matches);
    return 0;
}
