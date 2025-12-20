#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>

static int row_open;
static char *scratch;
static size_t scratch_cap;

static void emit(const quill_field *f)
{
    size_t n, i;
    const char *v;
    if (f->len > scratch_cap) {
        scratch_cap = f->len * 2;
        scratch = (char *)realloc(scratch, scratch_cap);
        if (!scratch) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    v = quill_value(f, scratch, &n);
    fputs(row_open ? "," : "[", stdout);
    putchar('"');
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)v[i];
        if (c == '"' || c == '\\')
            printf("\\%c", c);
        else if (c < 0x20 || c >= 0x7F)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
    row_open = !f->eol;
    if (f->eol)
        fputs("]\n", stdout);
}

static char *load(FILE *f, size_t *len)
{
    size_t cap = 1 << 20, n = 0;
    char *s = NULL, *t;
    for (;;) {
        if (!(t = (char *)realloc(s, cap))) {
            free(s);
            return NULL;
        }
        s = t;
        n += fread(s + n, 1, cap - n, f);
        if (n < cap)
            break;
        cap *= 2;
    }
    *len = n;
    return ferror(f) ? (free(s), (char *)NULL) : s;
}

int main(int argc, char **argv)
{
    static quill_parser p;
    quill_field f;
    size_t len;
    char *buf;
    FILE *in;
    if (argc != 2 || !(in = fopen(argv[1], "rb"))) {
        fputs("usage: dump FILE\n", stderr);
        return 2;
    }
    buf = load(in, &len);
    fclose(in);
    if (!buf)
        return 2;
    quill_init(&p, ',');
    quill_feed(&p, buf, len, 1);
    while (quill_next(&p, &f) == QUILL_FIELD)
        emit(&f);
    if (row_open)
        fputs("]\n", stdout);
    free(scratch);
    free(buf);
    return 0;
}
