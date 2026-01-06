#define QUILL_I_CAP 128
#define QUILL_IMPLEMENTATION
#include "quill.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    unsigned char *b;
    size_t n, cap;
    const char *msg;
    uint64_t line, col, off;
} result;

static void put(result *r, const void *p, size_t n)
{
    if (r->n + n > r->cap) {
        r->cap = (r->n + n) * 2 + 64;
        r->b = (unsigned char *)realloc(r->b, r->cap);
        assert(r->b);
    }
    if (n)
        memcpy(r->b + r->n, p, n);
    r->n += n;
}

static void put_field(result *r, const char *v, size_t len, int eol)
{
    unsigned char e = (unsigned char)eol;
    put(r, &len, sizeof len);
    put(r, v, len);
    put(r, &e, 1);
}

static void oracle_field(result *r, const unsigned char *s, size_t a, size_t b, int eol)
{
    char *v = (char *)malloc(b - a + 1);
    size_t n = 0, j;
    int in = 1;
    if (eol && b > a && s[b - 1] == '\r')
        b--;
    if (b > a && s[a] == '"') {
        a++;
        if (b > a && s[b - 1] == '"')
            b--;
        for (j = a; j < b; j++) {
            if (s[j] != '"')
                v[n++] = (char)s[j];
            else if (in && j + 1 < b && s[j + 1] == '"')
                v[n++] = '"', j++;
            else
                in = !in;
        }
    } else {
        memcpy(v, s + a, b - a);
        n = b - a;
    }
    put_field(r, v, n, eol);
    free(v);
}

static void oracle_fail(result *r, const unsigned char *s, size_t at, size_t line0, const char *msg)
{
    size_t i, start = line0;
    r->line = 1;
    for (i = 0; i < at; i++)
        if (s[i] == '\n')
            r->line++, start = i + 1;
    r->col = at - start + 1;
    r->off = at;
    r->msg = msg;
}

static void oracle(const unsigned char *s, size_t len, unsigned char delim, int strict, result *r)
{
    size_t i = 0, fs, line0 = 0;
    int in = 0, psep = 1, pclose = 0, pcr = 0, row_start = 1;
    if (len >= 3 && !memcmp(s, "\xEF\xBB\xBF", 3))
        i = line0 = 3;
    for (fs = i; i < len; i++) {
        unsigned char c = s[i];
        int sep, close = 0;
        if (strict) {
            if (c == '"' && !in && !psep && !pclose) {
                oracle_fail(r, s, i, line0, "quote inside unquoted field");
                return;
            }
            if (pclose && c != '"' && c != delim && c != '\n' && c != '\r') {
                oracle_fail(r, s, i, line0, "unexpected byte after closing quote");
                return;
            }
            if (pcr && c != '\n') {
                oracle_fail(r, s, i, line0, "carriage return not followed by newline");
                return;
            }
        }
        if (c == '"')
            close = in, in = !in;
        sep = !in && (c == delim || c == '\n');
        pcr = !in && c == '\r';
        if (sep) {
            oracle_field(r, s, fs, i, c == '\n');
            row_start = c == '\n';
            fs = i + 1;
        }
        psep = sep;
        pclose = close;
    }
    if (strict && (in || pcr)) {
        oracle_fail(r, s, len, line0, in ? "unterminated quoted field" : "carriage return not followed by newline");
        return;
    }
    if (fs < len || !row_start)
        oracle_field(r, s, fs, len, 1);
}

static uint64_t rng = 88172645463325252ULL;

static uint64_t next_rand(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static quill_parser P;

static void collect(quill_field *f, result *r)
{
    char *tmp = (char *)malloc(f->len + 1);
    size_t n;
    const char *v = quill_value(f, tmp, &n);
    put_field(r, v, n, f->eol);
    free(tmp);
}

static void finish(quill_parser *p, int rc, result *r)
{
    if (rc == QUILL_ERROR) {
        r->msg = p->err_msg;
        r->line = p->err_line;
        r->col = p->err_col;
        r->off = p->err_offset;
    }
}

static void parse(const unsigned char *s, size_t len, char delim, int strict, size_t chunk, result *r)
{
    quill_field f;
    int rc;
    assert(quill_init(&P, delim, strict ? QUILL_STRICT : 0) == 0);
    if (!chunk) {
        quill_feed(&P, (const char *)s, len, 1);
        while ((rc = quill_next(&P, &f)) == QUILL_FIELD)
            collect(&f, r);
        assert(rc != QUILL_MORE);
        finish(&P, rc, r);
        return;
    }
    {
        char *buf = (char *)malloc(len + 1);
        size_t have = 0, pos = 0;
        int empty_last = (int)(next_rand() % 2);
        for (;;) {
            size_t take = 1 + (size_t)(next_rand() % chunk);
            int final;
            if (take > len - pos)
                take = len - pos;
            memcpy(buf + have, s + pos, take);
            have += take;
            pos += take;
            final = pos == len && !(empty_last && take);
            quill_feed(&P, buf, have, final);
            while ((rc = quill_next(&P, &f)) == QUILL_FIELD)
                collect(&f, r);
            if (rc != QUILL_MORE)
                break;
            assert(!final);
            have -= quill_consumed(&P);
            memmove(buf, buf + quill_consumed(&P), have);
        }
        finish(&P, rc, r);
        free(buf);
    }
}

static void check(const unsigned char *s, size_t len, char delim, int strict)
{
    result want = {0};
    size_t c;
    static const size_t chunks[] = {0, 1, 7, 64, 200, 5000};
    oracle(s, len, (unsigned char)delim, strict, &want);
    for (c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
        result got = {0};
        parse(s, len, delim, strict, chunks[c], &got);
        if (got.n != want.n || (want.n && memcmp(got.b, want.b, want.n)) || !got.msg != !want.msg
            || (want.msg && strcmp(got.msg, want.msg)) || got.line != want.line || got.col != want.col) {
            fprintf(stderr, "mismatch: chunk %zu strict %d delim 0x%02x len %zu\n", chunks[c], strict,
                    (unsigned char)delim, len);
            abort();
        }
        free(got.b);
    }
    free(want.b);
}

static const char delims[] = {',', ';', '\t', 'a', (char)0xBB};

int main(void)
{
    static unsigned char buf[40000];
    static const unsigned char alpha[] = {'a', 'b', ',', ';', '\t', '"', '"', '\n', '\r', 0, 0xEF, 0xBB, 0xBF};
    int i;

    assert(quill_init(&P, '"', 0) < 0 && quill_init(&P, '\n', 0) < 0 && quill_init(&P, '\r', 0) < 0);
    assert(quill_init(&P, 0, 0) < 0);

    for (i = 0; i < 20000; i++) {
        size_t len = (size_t)(next_rand() % (i % 50 == 0 ? sizeof buf : 400)), j;
        char delim = delims[next_rand() % 5];
        int strict = (int)(next_rand() % 2);
        for (j = 0; j < len; j++)
            buf[j] = alpha[next_rand() % sizeof alpha];
        check(buf, len, delim, strict);
    }
    printf("ok, %d cases\n", i);
    return 0;
}
