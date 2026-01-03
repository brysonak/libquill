/* quill, a fast CSV parser */

#ifndef QUILL_H
#define QUILL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QUILL_I_CAP    4096
#define QUILL_I_BLOCKS (QUILL_I_CAP / 64)

#define QUILL_INLINE static inline

enum { QUILL_ERROR = -1, QUILL_END = 0, QUILL_FIELD = 1, QUILL_MORE = 2 };
enum { QUILL_STRICT = 1 };

typedef struct {
    const char *ptr; /* raw bytes, outer quotes removed */
    size_t len;
    int quoted; /* may hold "" escapes  */
    int eol;
} quill_field;

typedef struct {
    uint64_t inq, sep, close, cr;
    uint64_t lines, nl_mask, nl_at, gbase, err_at;
    const char *err;
    int strict;
} quill_i_state;

typedef struct quill_parser {
    const char *buf, *end, *scan, *field, *base;
    const uint32_t *ip, *ie;
    uint64_t off0, line0;
    uint32_t n;
    unsigned char delim, final, done;
    quill_i_state st;
    uint64_t err_line, err_col, err_offset;
    const char *err_msg;
    uint32_t idx[QUILL_I_CAP];
} quill_parser;

int quill_init(quill_parser *p, char delim, int flags);
void quill_feed(quill_parser *p, const char *buf, size_t len, int final);
int quill_i_next_slow(quill_parser *p, quill_field *f);
const char *quill_i_unescape(const quill_field *f, char *dst, size_t *len);

QUILL_INLINE void quill_i_emit(quill_parser *p, quill_field *f, const char *e, int eol)
{
    const char *s = p->field;
    p->field = e + 1;
    if (eol && e > s && e[-1] == '\r')
        e--;
    f->quoted = *s == '"';
    if (f->quoted) {
        s++;
        if (e[-1] == '"')
            e--;
    }
    f->ptr = s;
    f->len = (size_t)(e - s);
    f->eol = eol;
}

QUILL_INLINE int quill_next(quill_parser *p, quill_field *f)
{
    if (p->ip < p->ie) {
        const char *e = p->base + *p->ip++;
        quill_i_emit(p, f, e, (unsigned char)*e != p->delim);
        return QUILL_FIELD;
    }
    return quill_i_next_slow(p, f);
}

QUILL_INLINE size_t quill_consumed(const quill_parser *p)
{
    return (size_t)(p->field - p->buf);
}

/* Points into the input when nothing needs unescaping, else into dst */
QUILL_INLINE const char *quill_value(const quill_field *f, char *dst, size_t *len)
{
    if (!f->quoted) {
        *len = f->len;
        return f->ptr;
    }
    return quill_i_unescape(f, dst, len);
}

#ifdef __cplusplus
}
#endif
#endif

#ifdef QUILL_IMPLEMENTATION
#ifndef QUILL_IMPLEMENTATION_DONE
#define QUILL_IMPLEMENTATION_DONE

#if defined(__GNUC__) || defined(__clang__)
#define QUILL_I_INLINE static inline __attribute__((always_inline))
#else
#define QUILL_I_INLINE static inline
#endif

QUILL_I_INLINE unsigned quill_i_popcount(uint64_t x)
{
    return (unsigned)__builtin_popcountll(x);
}

QUILL_I_INLINE unsigned quill_i_ctz(uint64_t x)
{
    return (unsigned)__builtin_ctzll(x);
}

QUILL_I_INLINE uint64_t quill_i_pxor(uint64_t m)
{
    m ^= m << 1;
    m ^= m << 2;
    m ^= m << 4;
    m ^= m << 8;
    m ^= m << 16;
    m ^= m << 32;
    return m;
}

QUILL_I_INLINE void quill_i_index(uint32_t **out, uint32_t off, uint64_t s)
{
    uint32_t *o = *out;
    while (s) {
        *o++ = off + quill_i_ctz(s);
        s &= s - 1;
    }
    *out = o;
}

QUILL_I_INLINE int quill_i_block(quill_i_state *t, uint32_t **out, uint32_t off, uint64_t q, uint64_t pq, uint64_t d,
                                 uint64_t n, uint64_t r)
{
    uint64_t s;
    pq ^= t->inq;
    t->inq = 0 - (pq >> 63);
    s = (d | n) & ~pq;
    if (t->strict) {
        uint64_t close = q & ~pq, crout = r & ~pq;
        uint64_t pclose = close << 1 | t->close;
        uint64_t b1 = q & pq & ~(s << 1 | t->sep | pclose);
        uint64_t b2 = pclose & ~(q | d | n | r);
        uint64_t b3 = (crout << 1 | t->cr) & ~n;
        uint64_t bad = b1 | b2 | b3;
        t->sep = s >> 63;
        t->close = close >> 63;
        t->cr = crout >> 63;
        if (bad) {
            uint64_t low = bad & (0 - bad);
            s &= low - 1;
            n &= low - 1;
            t->err_at = t->gbase + off + quill_i_ctz(bad);
            t->err = (b1 & low) ? "quote inside unquoted field"
                   : (b2 & low) ? "unexpected byte after closing quote"
                                : "carriage return not followed by newline";
        }
        t->lines += quill_i_popcount(n);
        if (n) {
            t->nl_mask = n;
            t->nl_at = t->gbase + off;
        }
    }
    quill_i_index(out, off, s);
    return t->err ? 1 : 0;
}

static size_t quill_i_run_scalar(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off)
{
    quill_i_state t = p->st;
    uint32_t *o = p->idx + p->n;
    size_t k;
    for (k = 0; k < nb; k++, s += 64, off += 64) {
        uint64_t q = 0, d = 0, n = 0, r = 0;
        unsigned i;
        for (i = 0; i < 64; i++) {
            q |= (uint64_t)(s[i] == '"') << i;
            d |= (uint64_t)(s[i] == p->delim) << i;
            n |= (uint64_t)(s[i] == '\n') << i;
            r |= (uint64_t)(s[i] == '\r') << i;
        }
        if (quill_i_block(&t, &o, off, q, quill_i_pxor(q), d, n, r)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = (uint32_t)(o - p->idx);
    return k;
}

int quill_init(quill_parser *p, char delim, int flags)
{
    memset(p, 0, offsetof(quill_parser, idx));
    p->delim = (unsigned char)delim;
    p->st.sep = 1;
    p->st.strict = (flags & QUILL_STRICT) != 0;
    return 0;
}

void quill_feed(quill_parser *p, const char *buf, size_t len, int final)
{
    size_t keep = p->buf ? (size_t)(p->scan - p->field) : 0;
    if (p->buf)
        p->off0 += (uint64_t)(p->field - p->buf);
    p->buf = p->field = buf;
    p->scan = buf + keep;
    p->end = buf + len;
    p->final = (unsigned char)(final != 0);
    p->n = 0;
    p->ip = p->ie = p->idx;
    if (len >= 3 && memcmp(buf, "\xEF\xBB\xBF", 3) == 0) {
        p->field = p->scan = buf + 3;
        p->line0 = 3;
    }
}

static void quill_i_scan(quill_parser *p)
{
    const unsigned char *s = (const unsigned char *)p->scan, *end = (const unsigned char *)p->end;
    size_t nb = (size_t)(end - s) / 64, k;
    if (nb > QUILL_I_BLOCKS)
        nb = QUILL_I_BLOCKS;
    p->base = (const char *)s;
    p->n = 0;
    p->ip = p->ie = p->idx;
    p->st.gbase = p->off0 + (uint64_t)((const char *)s - p->buf);
    k = quill_i_run_scalar(p, s, nb, 0);
    s += 64 * k;
    if (k == nb && p->final && !p->st.err && s < end && (size_t)(end - s) < 64) {
        unsigned char pad[64] = {0};
        size_t rem = (size_t)(end - s);
        memcpy(pad, s, rem);
        quill_i_run_scalar(p, pad, 1, (uint32_t)(s - (const unsigned char *)p->base));
        s = end;
    }
    p->scan = (const char *)s;
}

static int quill_i_fail(quill_parser *p, uint64_t at, const char *msg)
{
    uint64_t m = p->st.nl_mask, start = p->line0;
    if (m) {
        unsigned b = 63;
        while (!(m >> b & 1))
            b--;
        start = p->st.nl_at + b + 1;
    }
    p->err_offset = at;
    p->err_line = p->st.lines + 1;
    p->err_col = at - start + 1;
    p->err_msg = msg;
    p->done = 1;
    return QUILL_ERROR;
}

int quill_i_next_slow(quill_parser *p, quill_field *f)
{
    if (p->err_msg)
        return QUILL_ERROR;
    if (p->st.err)
        return quill_i_fail(p, p->st.err_at, p->st.err);
    while (p->final ? p->scan < p->end : p->end - p->scan >= 64) {
        quill_i_scan(p);
        if (p->n) {
            p->ip = p->idx + 1;
            p->ie = p->idx + p->n;
            const char *e = p->base + p->idx[0];
            quill_i_emit(p, f, e, (unsigned char)*e != p->delim);
            return QUILL_FIELD;
        }
        if (p->st.err)
            return quill_i_fail(p, p->st.err_at, p->st.err);
    }
    if (!p->final)
        return QUILL_MORE;
    if (p->done)
        return QUILL_END;
    p->done = 1;
    if (p->st.strict) {
        uint64_t at = p->off0 + (uint64_t)(p->end - p->buf);
        if (p->st.inq)
            return quill_i_fail(p, at, "unterminated quoted field");
        if (p->end > p->buf && p->end[-1] == '\r')
            return quill_i_fail(p, at, "carriage return not followed by newline");
    }
    if (p->field < p->end || (p->field > p->buf && p->field[-1] == p->delim)) {
        quill_i_emit(p, f, p->end, 1);
        p->field = p->end;
        return QUILL_FIELD;
    }
    return QUILL_END;
}

const char *quill_i_unescape(const quill_field *f, char *dst, size_t *len)
{
    const char *s = f->ptr, *e = f->ptr + f->len, *q;
    char *d = dst;
    int in = 1;
    while ((q = (const char *)memchr(s, '"', (size_t)(e - s))) != NULL) {
        memcpy(d, s, (size_t)(q - s));
        d += q - s;
        if (in && q + 1 < e && q[1] == '"') {
            *d++ = '"';
            s = q + 2;
        } else {
            in = !in;
            s = q + 1;
        }
    }
    memcpy(d, s, (size_t)(e - s));
    *len = (size_t)(d - dst) + (size_t)(e - s);
    return dst;
}

#endif
#endif
