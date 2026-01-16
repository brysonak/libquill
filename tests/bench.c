#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
    size_t len, k, cap = 0;
    char *data, *tmp = NULL;
    if (argc != 2) {
        fputs("usage: bench FILE\n", stderr);
        return 2;
    }
    if (!(data = load(argv[1], &len))) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    printf("size %zu\n", len);
    for (k = 0; k <= (size_t)quill_i_best(); k++) {
        size_t fields = 0, iters = 0;
        double t0 = now(), t;
        do {
            quill_field f;
            quill_init(&p, ',', 0);
            p.run = quill_i_kernels[k].run;
            quill_feed(&p, data, len, 1);
            fields = 0;
            while (quill_next(&p, &f) == QUILL_FIELD) {
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
        } while (iters < 10);
        t = now() - t0;
        printf("kernel %s %.4f GB/s %zu fields\n", quill_i_kernels[k].name, (double)len * (double)iters / t / 1e9,
               fields);
    }
    free(data);
    free(tmp);
    return 0;
}
