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

enum { QUILL_END = 0, QUILL_FIELD = 1 };

typedef struct {
    const char *ptr;
    size_t len;
    int quoted;
    int eol;
} quill_field;

typedef struct quill_parser {
    const char *buf, *end, *scan, *field, *base;
    const uint32_t *ip, *ie;
    uint32_t n;
    unsigned char delim, done;
    uint64_t inq;
    uint32_t idx[QUILL_I_CAP];
} quill_parser;

int quill_init(quill_parser *p, char delim);
void quill_feed(quill_parser *p, const char *buf, size_t len);
int quill_next(quill_parser *p, quill_field *f);
const char *quill_value(const quill_field *f, char *dst, size_t *len);

#ifdef __cplusplus
}
#endif
#endif

#ifdef QUILL_IMPLEMENTATION
#ifndef QUILL_IMPLEMENTATION_DONE
#define QUILL_IMPLEMENTATION_DONE

#define QUILL_I_INLINE static inline

QUILL_I_INLINE unsigned quill_i_ctz(uint64_t x)
{
    return (unsigned)__builtin_ctzll(x);
}

QUILL_I_INLINE uint64_t quill_i_prefix_xor(uint64_t m)
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

QUILL_I_INLINE void quill_i_block(uint64_t *inq, uint32_t **out, uint32_t off, uint64_t pq, uint64_t d, uint64_t n)
{
    pq ^= *inq;
    *inq = 0 - (pq >> 63);
    quill_i_index(out, off, (d | n) & ~pq);
}

static size_t quill_i_run_scalar(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off)
{
    uint32_t *o = p->idx + p->n;
    size_t k;
    for (k = 0; k < nb; k++, s += 64, off += 64) {
        uint64_t quote = 0, delim = 0, nl = 0;
        unsigned i;
        for (i = 0; i < 64; i++) {
            quote |= (uint64_t)(s[i] == '"') << i;
            delim |= (uint64_t)(s[i] == p->delim) << i;
            nl |= (uint64_t)(s[i] == '\n') << i;
        }
        quill_i_block(&p->inq, &o, off, quill_i_prefix_xor(quote), delim, nl);
    }
    p->n = (uint32_t)(o - p->idx);
    return k;
}

int quill_init(quill_parser *p, char delim)
{
    memset(p, 0, offsetof(quill_parser, idx));
    p->delim = (unsigned char)delim;
    return 0;
}

void quill_feed(quill_parser *p, const char *buf, size_t len)
{
    p->buf = p->field = p->scan = buf;
    p->end = buf + len;
    p->n = 0;
    p->ip = p->ie = p->idx;
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
    k = quill_i_run_scalar(p, s, nb, 0);
    s += 64 * k;
    if (k == nb && s < end && (size_t)(end - s) < 64) {
        unsigned char pad[64] = {0};
        size_t rem = (size_t)(end - s);
        memcpy(pad, s, rem);
        quill_i_run_scalar(p, pad, 1, (uint32_t)(s - (const unsigned char *)p->base));
        s = end;
    }
    p->scan = (const char *)s;
}

static void quill_i_emit(quill_parser *p, quill_field *f, const char *e, int eol)
{
    const char *s = p->field;
    p->field = e + 1;
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

int quill_next(quill_parser *p, quill_field *f)
{
    if (p->ip < p->ie) {
        const char *e = p->base + *p->ip++;
        quill_i_emit(p, f, e, (unsigned char)*e != p->delim);
        return QUILL_FIELD;
    }
    while (p->scan < p->end) {
        quill_i_scan(p);
        if (p->n) {
            p->ip = p->idx + 1;
            p->ie = p->idx + p->n;
            const char *e = p->base + p->idx[0];
            quill_i_emit(p, f, e, (unsigned char)*e != p->delim);
            return QUILL_FIELD;
        }
    }
    if (p->done)
        return QUILL_END;
    p->done = 1;
    if (p->field < p->end || (p->field > p->buf && p->field[-1] == p->delim)) {
        quill_i_emit(p, f, p->end, 1);
        p->field = p->end;
        return QUILL_FIELD;
    }
    return QUILL_END;
}

const char *quill_value(const quill_field *f, char *dst, size_t *len)
{
    const char *s = f->ptr, *e = f->ptr + f->len, *q;
    char *d = dst;
    int in = 1;
    if (!f->quoted) {
        *len = f->len;
        return f->ptr;
    }
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
