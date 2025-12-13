#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>

static int row_open;

static void emit(const quill_field *f)
{
    size_t i;
    fputs(row_open ? "," : "[", stdout);
    putchar('"');
    for (i = 0; i < f->len; i++) {
        unsigned char c = (unsigned char)f->ptr[i];
        if (c == '"' || c == '\\')
            printf("\\%c", c);
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
    quill_feed(&p, buf, len);
    while (quill_next(&p, &f) == QUILL_FIELD)
        emit(&f);
    if (row_open)
        fputs("]\n", stdout);
    free(buf);
    return 0;
}
