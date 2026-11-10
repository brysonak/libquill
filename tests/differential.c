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

static uint64_t guesses, wrong_guesses;

static void parse_ranges(quill_i_run_fn run, const unsigned char *s, size_t len, char delim, int strict, result *r)
{
    quill_range rg[8];
    result part[8];
    size_t n = 1 + (size_t)(next_rand() % 8), k, cut = 0;
    int guess = !strict && next_rand() % 2;
    memset(part, 0, sizeof part);
    for (k = 0; k < n; k++) {
        rg[k].start = cut;
        if (k + 1 == n)
            cut = len;
        else if (next_rand() % 3 == 0)
            cut += (size_t)(next_rand() % 4);
        else
            cut += (size_t)(next_rand() % (len / n * 2 + 1));
        if (cut > len)
            cut = len;
        rg[k].end = cut;
        if (!guess)
            quill_i_count(quill_i_kernels[next_rand() % (size_t)(quill_i_best() + 1)].count, (const char *)s, &rg[k]);
    }
    for (k = 0; k < n; k++) {
        quill_field f;
        int rc;
        if (guess)
            assert(quill_init_guess(&P, delim, 0, (const char *)s, len, rg, k) == 0);
        else
            assert(quill_init_range(&P, delim, strict ? QUILL_STRICT : 0, (const char *)s, len, rg, k) == 0);
        P.run = run;
        while ((rc = quill_next(&P, &f)) == QUILL_FIELD)
            collect(&f, &part[k]);
        assert(rc == QUILL_END || rc == QUILL_ERROR);
        if (rc == QUILL_ERROR) {
            finish(&P, rc, &part[k]);
            n = k + 1;
            break;
        }
    }
    for (k = 0; k < n; k++) {
        if (guess) {
            guesses++;
            if (!quill_guess_ok(rg, k)) {
                quill_field f;
                wrong_guesses++;
                part[k].n = 0;
                assert(quill_init_range(&P, delim, 0, (const char *)s, len, rg, k) == 0);
                P.run = run;
                while (quill_next(&P, &f) == QUILL_FIELD)
                    collect(&f, &part[k]);
            }
        }
        put(r, part[k].b, part[k].n);
        if (part[k].msg) {
            r->msg = part[k].msg;
            r->line = part[k].line;
            r->col = part[k].col;
            r->off = part[k].off;
        }
        free(part[k].b);
    }
    for (; k < 8; k++)
        free(part[k].b);
}

static void parse(quill_i_run_fn run, const unsigned char *s, size_t len, char delim, int strict, size_t chunk,
                  result *r)
{
    quill_field f;
    int rc;
    if (chunk == (size_t)-1) {
        parse_ranges(run, s, len, delim, strict, r);
        return;
    }
    assert(quill_init(&P, delim, strict ? QUILL_STRICT : 0) == 0);
    P.run = run;
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

static uint64_t rows_of(const result *r)
{
    uint64_t rows = 0;
    size_t at = 0, n;
    while (at < r->n) {
        memcpy(&n, r->b + at, sizeof n);
        at += sizeof n + n;
        rows += r->b[at++];
    }
    return rows;
}

static void check_rows(const unsigned char *s, size_t len, uint64_t want)
{
    quill_range rg[8];
    size_t n = 1 + (size_t)(next_rand() % 8), k, cut = 0;
    quill_i_count_fn count = quill_i_kernels[next_rand() % (size_t)(quill_i_best() + 1)].count;
    for (k = 0; k < n; k++) {
        rg[k].start = cut;
        cut = k + 1 == n ? len : cut + (size_t)(next_rand() % (len / n * 2 + 1));
        rg[k].end = cut > len ? len : cut;
        cut = rg[k].end;
        quill_i_count(count, (const char *)s, &rg[k]);
    }
    if (quill_count_rows((const char *)s, len, QUILL_I_NULL, 0) != want
        || quill_count_rows((const char *)s, len, rg, n) != want) {
        fprintf(stderr, "row count mismatch: len %zu ranges %zu want %llu\n", len, n, (unsigned long long)want);
        abort();
    }
}

static void check(const unsigned char *s, size_t len, char delim, int strict)
{
    result want = {0};
    size_t k, levels = sizeof quill_i_kernels / sizeof quill_i_kernels[0];
    static const size_t chunks[] = {0, 1, 7, 64, 200, 5000, (size_t)-1, (size_t)-1, (size_t)-1};
    oracle(s, len, (unsigned char)delim, strict, &want);
    if (!strict)
        check_rows(s, len, rows_of(&want));
    for (k = 0; k < levels && quill_i_kernels[k].level <= quill_i_level(); k++) {
        size_t c;
        for (c = 0; c < sizeof chunks / sizeof chunks[0]; c++) {
            result got = {0};
            parse(quill_i_kernels[k].run, s, len, delim, strict, chunks[c], &got);
            if (got.n != want.n || (want.n && memcmp(got.b, want.b, want.n)) || !got.msg != !want.msg
                || (want.msg && strcmp(got.msg, want.msg)) || got.line != want.line || got.col != want.col
                || got.off != want.off) {
                fprintf(stderr, "mismatch: kernel %s chunk %zu strict %d delim 0x%02x len %zu\n",
                        quill_i_kernels[k].name, chunks[c], strict, (unsigned char)delim, len);
                fprintf(stderr, "want %s %llu:%llu, got %s %llu:%llu\n", want.msg ? want.msg : "ok",
                        (unsigned long long)want.line, (unsigned long long)want.col, got.msg ? got.msg : "ok",
                        (unsigned long long)got.line, (unsigned long long)got.col);
                abort();
            }
            free(got.b);
        }
    }
    free(want.b);
}

static const char delims[] = {',', ';', '\t', 'a', (char)0xBB};

#ifdef QUILL_FUZZ
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1)
        return 0;
    rng = 88172645463325252ULL ^ data[0];
    check(data + 1, size - 1, delims[data[0] % 5], data[0] >> 7);
    return 0;
}
#else

static size_t gen_valid(unsigned char *s, size_t cap, char delim)
{
    static const char text[] = "abc xyz 0123";
    size_t n = 0;
    if (next_rand() % 8 == 0)
        memcpy(s, "\xEF\xBB\xBF", 3), n = 3;
    while (n + 200 < cap) {
        unsigned fields = 1 + (unsigned)(next_rand() % 6), i;
        for (i = 0; i < fields; i++) {
            unsigned len = (unsigned)(next_rand() % 12), j;
            int quoted = next_rand() % 3 == 0;
            if (quoted)
                s[n++] = '"';
            for (j = 0; j < len; j++) {
                unsigned c = (unsigned)(next_rand() % 16);
                if (quoted && c == 0)
                    s[n++] = '"', s[n++] = '"';
                else if (quoted && c == 1)
                    s[n++] = (unsigned char)delim;
                else if (quoted && c == 2)
                    s[n++] = '\n';
                else if (quoted && c == 3)
                    s[n++] = '\r';
                else
                    s[n++] = (unsigned char)text[c % (sizeof text - 1)];
                if ((unsigned char)s[n - 1] == (unsigned char)delim && !quoted)
                    s[n - 1] = 'z';
            }
            if (quoted)
                s[n++] = '"';
            if (i + 1 < fields)
                s[n++] = (unsigned char)delim;
        }
        if (next_rand() % 2)
            s[n++] = '\r';
        s[n++] = '\n';
    }
    if (n && next_rand() % 3 == 0)
        n--;
    if (n && next_rand() % 20 == 0)
        s[next_rand() % n] = '"';
    return n;
}

static void known(const char *in, int strict, const char *want, uint64_t line, uint64_t col)
{
    char out[256] = {0};
    quill_field f;
    int rc;
    assert(quill_init(&P, ',', strict ? QUILL_STRICT : 0) == 0);
    quill_feed(&P, in, strlen(in), 1);
    while ((rc = quill_next(&P, &f)) == QUILL_FIELD) {
        char tmp[64];
        size_t n;
        const char *v = quill_value(&f, tmp, &n);
        strncat(out, v, n);
        strcat(out, f.eol ? "|\n" : "|");
    }
    if (strcmp(out, want) != 0) {
        fprintf(stderr, "known case %s: got [%s] want [%s]\n", in, out, want);
        abort();
    }
    assert(rc == (line ? QUILL_ERROR : QUILL_END));
    assert(P.err_line == line && P.err_col == col);
}

int main(void)
{
    static unsigned char buf[40000];
    static const unsigned char alpha[] = {'a', 'b', ',', ';', '\t', '"', '"', '\n', '\r', 0, 0xEF, 0xBB, 0xBF};
    int i;

    assert(quill_init(&P, '"', 0) < 0 && quill_init(&P, '\n', 0) < 0 && quill_init(&P, '\r', 0) < 0);
    assert(quill_init(&P, 0, 0) < 0);
    {
        quill_range g = {0, 3, 0, 0, 0, 0};
        assert(quill_init_guess(&P, ',', QUILL_STRICT, "abc", 3, &g, 0) < 0);
    }
    {
        quill_range bad = {2, 1, 0, 0, 0, 0};
        assert(quill_init_range(&P, ',', 0, "abc", 3, &bad, 0) < 0);
        bad.start = 0;
        bad.end = 4;
        assert(quill_init_range(&P, ',', 0, "abc", 3, &bad, 0) < 0);
    }
    known("a,\"b,c\"\r\n\"x\"\"y\",", 0, "a|b,c|\nx\"y||\n", 0, 0);
    known("\xEF\xBB\xBF"
          "a\n\nb",
          1, "a|\n|\nb|\n", 0, 0);
    known("a,b\nc\"d", 1, "a|b|\n", 2, 2);
    known("\"a\"x", 1, "", 1, 4);
    known("\"a\n", 1, "", 2, 1);
    known("a\rb", 1, "", 1, 3);

    for (i = 0; i < 20000; i++) {
        size_t len = (size_t)(next_rand() % (i % 50 == 0 ? sizeof buf : 400)), j;
        char delim = delims[next_rand() % 5];
        int strict = (int)(next_rand() % 2);
        if (i % 2) {
            len = gen_valid(buf, len, delim);
        } else {
            for (j = 0; j < len; j++)
                buf[j] = alpha[next_rand() % sizeof alpha];
        }
        check(buf, len, delim, strict);
    }
    printf("ok, %d cases, %llu of %llu range guesses wrong and re-parsed\n", i, (unsigned long long)wrong_guesses,
           (unsigned long long)guesses);
    return 0;
}
#endif
