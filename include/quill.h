/* quill, a fast CSV parser */

#ifndef QUILL_H
#define QUILL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef QUILL_I_CAP
#define QUILL_I_CAP 4096
#endif
#define QUILL_I_BLOCKS (QUILL_I_CAP / 4)

#if defined(_MSC_VER) && !defined(__cplusplus)
#define QUILL_INLINE static __inline
#else
#define QUILL_INLINE static inline
#endif

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
    const char *origin;
    size_t (*run)(struct quill_parser *, const unsigned char *, size_t, uint32_t, uint64_t);
    uint64_t off0, line0;
    uint32_t n;
    unsigned char delim, final, done, row_start, sparse;
    quill_i_state st;
    uint64_t err_line, err_col, err_offset;
    const char *err_msg;
    uint32_t idx[QUILL_I_CAP];
} quill_parser;

int quill_init(quill_parser *p, char delim, int flags);
void quill_feed(quill_parser *p, const char *buf, size_t len, int final);
int quill_i_next_slow(quill_parser *p, quill_field *f);
const char *quill_i_unescape(const quill_field *f, char *dst, size_t *len);

QUILL_INLINE void quill_i_emit(quill_parser *p, quill_field *f, const char *e, int eol, int last)
{
    const char *s = p->field;
    p->field = e + 1;
    if (eol && e > s && e[-1] == '\r')
        e--;
    f->quoted = (!last || s < e) && *s == '"';
    if (f->quoted && ++s < e && e[-1] == '"')
        e--;
    f->ptr = s;
    f->len = (size_t)(e - s);
    f->eol = eol;
}

QUILL_INLINE int quill_next(quill_parser *p, quill_field *f)
{
    if (p->ip < p->ie) {
        const char *e = p->base + *p->ip++;
        quill_i_emit(p, f, e, (unsigned char)*e != p->delim, 0);
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

#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#if defined(__x86_64__) || defined(_M_X64)
#define QUILL_I_X64 1
#include <immintrin.h>
#endif
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define QUILL_I_INLINE    static inline __attribute__((always_inline))
#define QUILL_I_TARGET(x) __attribute__((target(x)))
#elif defined(_MSC_VER)
#define QUILL_I_INLINE static __forceinline
#define QUILL_I_TARGET(x)
#else
#define QUILL_I_INLINE static inline
#define QUILL_I_TARGET(x)
#endif

QUILL_I_INLINE unsigned quill_i_popcount(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_popcountll(x);
#else
    x -= (x >> 1) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (unsigned)((x * 0x0101010101010101ULL) >> 56);
#endif
}

/* x must be nonzero */
QUILL_I_INLINE unsigned quill_i_ctz(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(x);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    _BitScanForward64(&i, x);
    return (unsigned)i;
#else
    static const unsigned char debruijn[64] = {0,  1,  2,  53, 3,  7,  54, 27, 4,  38, 41, 8,  34, 55, 48, 28,
                                               62, 5,  39, 46, 44, 42, 22, 9,  24, 35, 59, 56, 49, 18, 29, 11,
                                               63, 52, 6,  26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
                                               51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12};
    return debruijn[((x & (0 - x)) * 0x022FDD63CC95386DULL) >> 58];
#endif
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

/* Up to 16 unconditional stores keep the common case branch free, so the index
   needs 64 spare slots past the last block that starts */
QUILL_I_INLINE void quill_i_flatten(uint32_t **out, uint32_t off, uint64_t s)
{
    uint32_t *o = *out;
    unsigned cnt, i;
    if (!s)
        return;
    cnt = quill_i_popcount(s);
    for (i = 0; i < 4; i++) {
        o[i] = off + quill_i_ctz(s | (1ULL << 63));
        s &= s - 1;
    }
    if (cnt > 4) {
        for (; i < 8; i++) {
            o[i] = off + quill_i_ctz(s | (1ULL << 63));
            s &= s - 1;
        }
        for (; s; i++) {
            o[i] = off + quill_i_ctz(s);
            s &= s - 1;
        }
    }
    *out = o + cnt;
}

QUILL_I_INLINE int quill_i_block(quill_i_state *t, uint32_t **out, uint32_t off, uint64_t q, uint64_t pq, uint64_t d,
                                 uint64_t n, uint64_t r, uint64_t valid)
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
        uint64_t bad = (b1 | b2 | b3) & valid;
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
    quill_i_flatten(out, off, s);
    return t->err ? 1 : 0;
}

static size_t quill_i_run_scalar(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    quill_i_state t = p->st;
    uint32_t *o = p->idx + p->n, *lim = p->idx + QUILL_I_CAP - 64;
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        uint64_t q = 0, d = 0, n = 0, r = 0;
        unsigned i;
        for (i = 0; i < 64; i++) {
            q |= (uint64_t)(s[i] == '"') << i;
            d |= (uint64_t)(s[i] == p->delim) << i;
            n |= (uint64_t)(s[i] == '\n') << i;
            r |= (uint64_t)(s[i] == '\r') << i;
        }
        if (quill_i_block(&t, &o, off, q, quill_i_pxor(q), d, n, r, valid)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = (uint32_t)(o - p->idx);
    return k;
}

#ifdef QUILL_I_X64
QUILL_I_INLINE uint64_t quill_i_mask_sse2(const __m128i *v, __m128i c)
{
    return (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v[0], c))
         | (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v[1], c)) << 16
         | (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v[2], c)) << 32
         | (uint64_t)(uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(v[3], c)) << 48;
}

/* Lenient blocks with no quote, delimiter or newline leave the state unchanged*/
QUILL_I_INLINE int quill_i_any_sse2(const __m128i *v, __m128i q, __m128i d, __m128i n)
{
    __m128i a = _mm_setzero_si128();
    int i;
    for (i = 0; i < 4; i++)
        a = _mm_or_si128(
            a, _mm_or_si128(_mm_cmpeq_epi8(v[i], q), _mm_or_si128(_mm_cmpeq_epi8(v[i], d), _mm_cmpeq_epi8(v[i], n))));
    return _mm_movemask_epi8(a);
}

QUILL_I_INLINE size_t quill_i_sse2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid,
                                   const int sparse)
{
    quill_i_state t = p->st;
    uint32_t *o = p->idx + p->n, *lim = p->idx + QUILL_I_CAP - 64;
    const __m128i Q = _mm_set1_epi8('"'), D = _mm_set1_epi8((char)p->delim);
    const __m128i N = _mm_set1_epi8('\n'), R = _mm_set1_epi8('\r');
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        __m128i v[4];
        uint64_t q;
        v[0] = _mm_loadu_si128((const __m128i *)s);
        v[1] = _mm_loadu_si128((const __m128i *)(s + 16));
        v[2] = _mm_loadu_si128((const __m128i *)(s + 32));
        v[3] = _mm_loadu_si128((const __m128i *)(s + 48));
        if (sparse && !quill_i_any_sse2(v, Q, D, N))
            continue;
        q = quill_i_mask_sse2(v, Q);
        if (quill_i_block(&t, &o, off, q, quill_i_pxor(q), quill_i_mask_sse2(v, D), quill_i_mask_sse2(v, N),
                          t.strict ? quill_i_mask_sse2(v, R) : 0, valid)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = (uint32_t)(o - p->idx);
    return k;
}

static size_t quill_i_run_sse2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    return p->sparse && !p->st.strict ? quill_i_sse2(p, s, nb, off, valid, 1) : quill_i_sse2(p, s, nb, off, valid, 0);
}

QUILL_I_TARGET("pclmul")
QUILL_I_INLINE uint64_t quill_i_clmul(uint64_t q)
{
    return (uint64_t)_mm_cvtsi128_si64(
        _mm_clmulepi64_si128(_mm_set_epi64x(0, (long long)q), _mm_set1_epi8((char)0xFF), 0));
}

QUILL_I_TARGET("avx2")
QUILL_I_INLINE uint64_t quill_i_mask_avx2(__m256i a, __m256i b, __m256i c)
{
    return (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(a, c))
         | (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, c)) << 32;
}

QUILL_I_TARGET("avx2")
QUILL_I_INLINE int quill_i_none_avx2(__m256i a, __m256i b, __m256i q, __m256i d, __m256i n)
{
    __m256i x = _mm256_or_si256(_mm256_or_si256(_mm256_cmpeq_epi8(a, q), _mm256_cmpeq_epi8(b, q)),
                                _mm256_or_si256(_mm256_cmpeq_epi8(a, d), _mm256_cmpeq_epi8(b, d)));
    x = _mm256_or_si256(x, _mm256_or_si256(_mm256_cmpeq_epi8(a, n), _mm256_cmpeq_epi8(b, n)));
    return _mm256_testz_si256(x, x);
}

QUILL_I_TARGET("avx2,popcnt,pclmul")
QUILL_I_INLINE size_t quill_i_avx2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid,
                                   const int sparse)
{
    quill_i_state t = p->st;
    uint32_t *o = p->idx + p->n, *lim = p->idx + QUILL_I_CAP - 64;
    const __m256i Q = _mm256_set1_epi8('"'), D = _mm256_set1_epi8((char)p->delim);
    const __m256i N = _mm256_set1_epi8('\n'), R = _mm256_set1_epi8('\r');
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        __m256i a = _mm256_loadu_si256((const __m256i *)s);
        __m256i b = _mm256_loadu_si256((const __m256i *)(s + 32));
        uint64_t q;
        if (sparse && quill_i_none_avx2(a, b, Q, D, N))
            continue;
        q = quill_i_mask_avx2(a, b, Q);
        if (quill_i_block(&t, &o, off, q, quill_i_clmul(q), quill_i_mask_avx2(a, b, D), quill_i_mask_avx2(a, b, N),
                          t.strict ? quill_i_mask_avx2(a, b, R) : 0, valid)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = (uint32_t)(o - p->idx);
    return k;
}

QUILL_I_TARGET("avx2,popcnt,pclmul")
static size_t quill_i_run_avx2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    return p->sparse && !p->st.strict ? quill_i_avx2(p, s, nb, off, valid, 1) : quill_i_avx2(p, s, nb, off, valid, 0);
}

static void quill_i_cpuid(unsigned leaf, unsigned r[4])
{
#if defined(_MSC_VER) && !defined(__clang__)
    int t[4];
    __cpuidex(t, (int)leaf, 0);
    memcpy(r, t, sizeof t);
#else
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
#endif
}

static uint64_t quill_i_xgetbv(void)
{
#if defined(_MSC_VER) && !defined(__clang__)
    return _xgetbv(0);
#else
    unsigned lo, hi;
    __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    return (uint64_t)hi << 32 | lo;
#endif
}

/* xgetbv confirms the OS saves the YMM registers */
static int quill_i_detect(void)
{
    unsigned r[4], ecx;
    uint64_t xcr;
    quill_i_cpuid(0, r);
    if (r[0] < 7)
        return 1;
    quill_i_cpuid(1, r);
    ecx = r[2];
    if (!(ecx >> 27 & 1) || !(ecx >> 28 & 1) || !(ecx >> 1 & 1) || !(ecx >> 23 & 1))
        return 1;
    xcr = quill_i_xgetbv();
    if ((xcr & 0x6) != 0x6)
        return 1;
    quill_i_cpuid(7, r);
    return (r[1] >> 5 & 1) ? 2 : 1;
}
#endif

typedef size_t (*quill_i_run_fn)(quill_parser *, const unsigned char *, size_t, uint32_t, uint64_t);

static const struct {
    const char *name;
    quill_i_run_fn run;
    int level;
} quill_i_kernels[] = {
    {"scalar", quill_i_run_scalar, 0},
#ifdef QUILL_I_X64
    {"sse2", quill_i_run_sse2, 1},
    {"avx2", quill_i_run_avx2, 2},
#endif
};

static int quill_i_level(void)
{
#ifdef QUILL_I_X64
    static int cached;
    if (!cached)
        cached = quill_i_detect();
    return cached;
#else
    return 0;
#endif
}

static int quill_i_best(void)
{
    int l = quill_i_level(), k = 0;
    while (k + 1 < (int)(sizeof quill_i_kernels / sizeof quill_i_kernels[0]) && quill_i_kernels[k + 1].level <= l)
        k++;
    return k;
}

int quill_init(quill_parser *p, char delim, int flags)
{
    unsigned char c = (unsigned char)delim;
    if (c == '"' || c == '\n' || c == '\r' || c == 0)
        return -1;
    memset(p, 0, offsetof(quill_parser, idx));
    p->run = quill_i_kernels[quill_i_best()].run;
    p->delim = c;
    p->row_start = 1;
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
    if (p->off0 == 0 && keep == 0 && len >= 3 && memcmp(buf, "\xEF\xBB\xBF", 3) == 0) {
        p->field = p->scan = buf + 3;
        p->line0 = 3;
    }
    p->origin = p->field;
}

static void quill_i_stage1(quill_parser *p)
{
    const unsigned char *s = (const unsigned char *)p->scan, *end = (const unsigned char *)p->end;
    size_t nb = (size_t)(end - s) / 64, k;
    if (nb > QUILL_I_BLOCKS)
        nb = QUILL_I_BLOCKS;
    p->base = (const char *)s;
    p->n = 0;
    p->ip = p->ie = p->idx;
    p->st.gbase = p->off0 + (uint64_t)((const char *)s - p->buf);
    k = p->run(p, s, nb, 0, ~(uint64_t)0);
    p->sparse = p->n < 2 * k;
    s += 64 * k;
    if (k == nb && p->final && !p->st.err && s < end && (size_t)(end - s) < 64) {
        unsigned char pad[64] = {0};
        size_t rem = (size_t)(end - s);
        memcpy(pad, s, rem);
        if (p->run(p, pad, 1, (uint32_t)(s - (const unsigned char *)p->base), ((uint64_t)1 << rem) - 1))
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

static int quill_i_row_start(const quill_parser *p)
{
    return p->field > p->origin ? (unsigned char)p->field[-1] != p->delim : p->row_start;
}

int quill_i_next_slow(quill_parser *p, quill_field *f)
{
    if (p->err_msg)
        return QUILL_ERROR;
    if (p->st.err)
        return quill_i_fail(p, p->st.err_at, p->st.err);
    while (p->final ? p->scan < p->end : p->end - p->scan >= 64) {
        quill_i_stage1(p);
        if (p->n) {
            p->ip = p->idx + 1;
            p->ie = p->idx + p->n;
            const char *e = p->base + p->idx[0];
            quill_i_emit(p, f, e, (unsigned char)*e != p->delim, 0);
            return QUILL_FIELD;
        }
        if (p->st.err)
            return quill_i_fail(p, p->st.err_at, p->st.err);
    }
    if (!p->final) {
        p->row_start = (unsigned char)quill_i_row_start(p);
        return QUILL_MORE;
    }
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
    if (p->field < p->end || !quill_i_row_start(p)) {
        quill_i_emit(p, f, p->end, 1, 1);
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
