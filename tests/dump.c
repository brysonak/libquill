#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <stdio.h>
#include <stdlib.h>

static int checksum;
static uint64_t rows, fields, hash = 14695981039346656037ULL;
static int row_open;
static char *scratch;
static size_t scratch_cap;

static void hash_bytes(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        hash = (hash ^ (unsigned char)s[i]) * 1099511628211ULL;
}

static void emit(const quill_field *f)
{
    size_t n, i;
    const char *v;
    char sep[1];
    if (f->len > scratch_cap) {
        scratch_cap = f->len * 2;
        scratch = (char *)realloc(scratch, scratch_cap);
        if (!scratch) {
            fprintf(stderr, "out of memory\n");
            exit(2);
        }
    }
    v = quill_value(f, scratch, &n);
    fields++;
    rows += (uint64_t)f->eol;
    if (checksum) {
        sep[0] = f->eol ? 0x1E : 0x1F;
        hash_bytes(v, n);
        hash_bytes(sep, 1);
        return;
    }
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

static int finish(const quill_parser *p, int rc)
{
    if (row_open && !checksum)
        fputs("]\n", stdout);
    free(scratch);
    if (checksum)
        printf("rows %llu fields %llu hash %016llx\n", (unsigned long long)rows, (unsigned long long)fields,
               (unsigned long long)hash);
    if (rc == QUILL_ERROR)
        printf("!error %llu %llu %s\n", (unsigned long long)p->err_line, (unsigned long long)p->err_col, p->err_msg);
    return 0;
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

static int usage(void)
{
    fputs("usage: dump [-s] [-d DELIM] [-k KERNEL] [-m "
          "whole|stream:N] [-c] FILE\n"
          "       dump -l\n",
          stderr);
    return 2;
}

int main(int argc, char **argv)
{
    static quill_parser p;
    quill_field f;
    const char *mode = "whole", *path = NULL;
    char delim = ',';
    int strict = 0, k = quill_i_best(), rc, i;
    size_t n = 0;
    FILE *in;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-l")) {
            for (k = 0; k <= quill_i_best(); k++)
                puts(quill_i_kernels[k].name);
            return 0;
        } else if (!strcmp(argv[i], "-s")) {
            strict = QUILL_STRICT;
        } else if (!strcmp(argv[i], "-c")) {
            checksum = 1;
        } else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
            delim = !strcmp(argv[++i], "tab") ? '\t' : argv[i][0];
        } else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            mode = argv[++i];
        } else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            for (k = quill_i_best(); k >= 0 && strcmp(quill_i_kernels[k].name, argv[i + 1]); k--) {
            }
            if (k < 0) {
                fprintf(stderr, "kernel %s not available\n", argv[i + 1]);
                return 2;
            }
            i++;
        } else if (argv[i][0] != '-' && !path) {
            path = argv[i];
        } else {
            return usage();
        }
    }
    if (!path || !(in = fopen(path, "rb")))
        return path ? (fprintf(stderr, "cannot open %s\n", path), 2) : usage();
    if (!strncmp(mode, "stream:", 7))
        n = (size_t)strtoul(mode + 7, NULL, 10);
    if (strcmp(mode, "whole") && !n)
        return usage();
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);

    if (!strncmp(mode, "stream:", 7)) {
        size_t cap = n, have = 0;
        char *buf = (char *)malloc(cap);
        if (!buf || quill_init(&p, delim, strict))
            return usage();
        p.run = quill_i_kernels[k].run;
        for (;;) {
            size_t want, got;
            int final;
            if (have == cap) {
                char *t = (char *)realloc(buf, cap *= 2);
                if (!t)
                    return 2;
                buf = t;
            }
            want = cap - have < n ? cap - have : n;
            got = fread(buf + have, 1, want, in);
            if (ferror(in))
                return 2;
            have += got;
            final = got < want;
            quill_feed(&p, buf, have, final);
            while ((rc = quill_next(&p, &f)) == QUILL_FIELD)
                emit(&f);
            if (rc != QUILL_MORE)
                break;
            have -= quill_consumed(&p);
            memmove(buf, buf + quill_consumed(&p), have);
        }
        free(buf);
        fclose(in);
        return finish(&p, rc);
    }

    {
        size_t len;
        char *buf = load(in, &len);
        fclose(in);
        if (!buf)
            return 2;
        if (quill_init(&p, delim, strict))
            return usage();
        p.run = quill_i_kernels[k].run;
        quill_feed(&p, buf, len, 1);
        while ((rc = quill_next(&p, &f)) == QUILL_FIELD)
            emit(&f);
        free(buf);
        return finish(&p, rc);
    }
}
