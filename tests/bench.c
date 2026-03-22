#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static char delim = ',';

static double now(void)
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
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
    free(data);
    free(tmp);
    return 0;
}
