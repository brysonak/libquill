/* quill, a fast CSV parser

   https://github.com/brysonak/libquill

   Copyright (c) 2026 Bryson Kelly

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
   permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
   the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE. */

/*
  Dear Ellie,
  I will never name my variables anything besides single letters. I hope you can forgive me
*/
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

#ifdef __cplusplus
#define QUILL_I_CAST(T, x) static_cast<T>(x)
#define QUILL_I_PTR(T, x)  static_cast<T>(static_cast<const void *>(x))
#else
#define QUILL_I_CAST(T, x) ((T)(x))
#define QUILL_I_PTR(T, x)  ((T)(const void *)(x))
#endif

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
    size_t start, end; /* set by the caller */
    int odd_quotes;
    int guess;
    uint64_t newlines, rows;
} quill_range;

typedef struct {
    uint64_t inq, sep, close, cr;
    uint64_t lines, nl_mask, nl_at, gbase, err_at;
    const char *err;
    int strict, guard;
} quill_i_state;

typedef struct quill_parser {
    const char *buf, *end, *scan, *field, *base;
    const uint32_t *ip, *ie;
    const char *origin;
    size_t (*run)(struct quill_parser *, const unsigned char *, size_t, uint32_t, uint64_t);
    uint64_t off0, line0;
    size_t stop;
    uint32_t n;
    unsigned char delim, final, done, row_start, halt, skip, sparse;
    quill_range *guessed;
    quill_i_state st;
    uint64_t err_line, err_col, err_offset;
    const char *err_msg;
    uint32_t idx[QUILL_I_CAP];
} quill_parser;

int quill_init(quill_parser *p, char delim, int flags);
void quill_feed(quill_parser *p, const char *buf, size_t len, int final);
void quill_count(const char *buf, quill_range *r);
int quill_init_range(quill_parser *p, char delim, int flags, const char *buf, size_t len, const quill_range *ranges,
                     size_t k);
int quill_init_guess(quill_parser *p, char delim, int flags, const char *buf, size_t len, quill_range *ranges,
                     size_t k);
int quill_guess_ok(const quill_range *ranges, size_t k);
uint64_t quill_count_rows(const char *buf, size_t len, const quill_range *ranges, size_t n);
int quill_i_next_slow(quill_parser *p, quill_field *f);
const char *quill_i_unescape(const quill_field *f, char *dst, size_t *len);

QUILL_INLINE void quill_i_emit(quill_parser *p, quill_field *f, const char *e, int eol, int last)
{
    const char *s = p->field;
    p->field = e + 1;
    if (eol) {
        if (e > s && e[-1] == '\r')
            e--;
        if (QUILL_I_CAST(size_t, p->field - p->buf) >= p->stop) {
            p->ie = p->ip;
            p->halt = 1;
        }
    }
    f->quoted = (!last || s < e) && *s == '"';
    if (f->quoted && ++s < e && e[-1] == '"')
        e--;
    f->ptr = s;
    f->len = QUILL_I_CAST(size_t, e - s);
    f->eol = eol;
}

QUILL_INLINE int quill_next(quill_parser *p, quill_field *f)
{
    if (p->ip < p->ie) {
        const char *e = p->base + *p->ip++;
        quill_i_emit(p, f, e, QUILL_I_CAST(unsigned char, *e) != p->delim, 0);
        return QUILL_FIELD;
    }
    return quill_i_next_slow(p, f);
}

QUILL_INLINE size_t quill_consumed(const quill_parser *p)
{
    return QUILL_I_CAST(size_t, p->field - p->buf);
}

/* Points into the input when nothing needs unescaping, else into dst */
QUILL_INLINE const char *quill_value(const quill_field *f, char *dst, size_t *len)
{
    if (!f->quoted || !memchr(f->ptr, '"', f->len)) {
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
#if (defined(__x86_64__) || defined(_M_X64)) && !defined(_M_ARM64EC)
#define QUILL_I_X64 1
#include <immintrin.h>
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !defined(__AARCH64EB__)
#define QUILL_I_NEON 1
#include <arm_neon.h>
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

/* MSVC headers define NULL as 0 in C++, which trips -Wzero-as-null-pointer-constant */
#if defined(__cplusplus) && (__cplusplus >= 201103L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201103L))
#define QUILL_I_NULL nullptr
#else
#define QUILL_I_NULL NULL
#endif

QUILL_I_INLINE unsigned quill_i_popcount(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return QUILL_I_CAST(unsigned, __builtin_popcountll(x));
#else
    x -= (x >> 1) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return QUILL_I_CAST(unsigned, (x * 0x0101010101010101ULL) >> 56);
#endif
}

/* x must be nonzero */
QUILL_I_INLINE unsigned quill_i_ctz(uint64_t x)
{
#if defined(__GNUC__) || defined(__clang__)
    return QUILL_I_CAST(unsigned, __builtin_ctzll(x));
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    _BitScanForward64(&i, x);
    return QUILL_I_CAST(unsigned, i);
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

typedef struct {
    uint64_t inq, lines, rows;
} quill_i_tally;

QUILL_I_INLINE void quill_i_tally_block(quill_i_tally *t, uint64_t pq, uint64_t n)
{
    pq ^= t->inq;
    t->inq = 0 - (pq >> 63);
    t->rows += quill_i_popcount(n & ~pq);
    t->lines += quill_i_popcount(n);
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
    } else if (t->guard) {
        uint64_t close = q & ~pq, pclose = close << 1 | t->close;
        uint64_t bad = q & pq & ~(s << 1 | t->sep | pclose) & valid;
        t->sep = s >> 63;
        t->close = close >> 63;
        if (bad) {
            s &= (bad & (0 - bad)) - 1;
            t->err_at = t->gbase + off + quill_i_ctz(bad);
            t->err = "quote inside unquoted field";
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
            q |= QUILL_I_CAST(uint64_t, s[i] == '"') << i;
            d |= QUILL_I_CAST(uint64_t, s[i] == p->delim) << i;
            n |= QUILL_I_CAST(uint64_t, s[i] == '\n') << i;
            r |= QUILL_I_CAST(uint64_t, s[i] == '\r') << i;
        }
        if (quill_i_block(&t, &o, off, q, quill_i_pxor(q), d, n, r, valid)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = QUILL_I_CAST(uint32_t, o - p->idx);
    return k;
}

#ifdef QUILL_I_X64
QUILL_I_INLINE uint64_t quill_i_mask_sse2(const __m128i *v, __m128i c)
{
    return QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm_movemask_epi8(_mm_cmpeq_epi8(v[0], c))))
         | QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm_movemask_epi8(_mm_cmpeq_epi8(v[1], c)))) << 16
         | QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm_movemask_epi8(_mm_cmpeq_epi8(v[2], c)))) << 32
         | QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm_movemask_epi8(_mm_cmpeq_epi8(v[3], c)))) << 48;
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
    const __m128i Q = _mm_set1_epi8('"'), D = _mm_set1_epi8(QUILL_I_CAST(char, p->delim));
    const __m128i N = _mm_set1_epi8('\n'), R = _mm_set1_epi8('\r');
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        __m128i v[4];
        uint64_t q;
        v[0] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s));
        v[1] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 16));
        v[2] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 32));
        v[3] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 48));
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
    p->n = QUILL_I_CAST(uint32_t, o - p->idx);
    return k;
}

static size_t quill_i_run_sse2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    return p->sparse && !p->st.strict ? quill_i_sse2(p, s, nb, off, valid, 1) : quill_i_sse2(p, s, nb, off, valid, 0);
}

static void quill_i_count_sse2(const unsigned char *s, size_t nb, quill_i_tally *t)
{
    const __m128i Q = _mm_set1_epi8('"'), N = _mm_set1_epi8('\n');
    for (; nb; nb--, s += 64) {
        __m128i v[4];
        uint64_t q;
        v[0] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s));
        v[1] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 16));
        v[2] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 32));
        v[3] = _mm_loadu_si128(QUILL_I_PTR(const __m128i *, s + 48));
        q = quill_i_mask_sse2(v, Q);
        quill_i_tally_block(t, q ? quill_i_pxor(q) : 0, quill_i_mask_sse2(v, N));
    }
}

QUILL_I_TARGET("pclmul")
QUILL_I_INLINE uint64_t quill_i_clmul(uint64_t q)
{
    return QUILL_I_CAST(uint64_t, _mm_cvtsi128_si64(_mm_clmulepi64_si128(_mm_set_epi64x(0, QUILL_I_CAST(long long, q)),
                                                                         _mm_set1_epi8(QUILL_I_CAST(char, 0xFF)), 0)));
}

QUILL_I_TARGET("avx2")
QUILL_I_INLINE uint64_t quill_i_mask_avx2(__m256i a, __m256i b, __m256i c)
{
    return QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, c))))
         | QUILL_I_CAST(uint64_t, QUILL_I_CAST(uint32_t, _mm256_movemask_epi8(_mm256_cmpeq_epi8(b, c)))) << 32;
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
    const __m256i Q = _mm256_set1_epi8('"'), D = _mm256_set1_epi8(QUILL_I_CAST(char, p->delim));
    const __m256i N = _mm256_set1_epi8('\n'), R = _mm256_set1_epi8('\r');
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        __m256i a = _mm256_loadu_si256(QUILL_I_PTR(const __m256i *, s));
        __m256i b = _mm256_loadu_si256(QUILL_I_PTR(const __m256i *, s + 32));
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
    p->n = QUILL_I_CAST(uint32_t, o - p->idx);
    return k;
}

QUILL_I_TARGET("avx2,popcnt,pclmul")
static size_t quill_i_run_avx2(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    return p->sparse && !p->st.strict ? quill_i_avx2(p, s, nb, off, valid, 1) : quill_i_avx2(p, s, nb, off, valid, 0);
}

QUILL_I_TARGET("avx2,popcnt,pclmul")
static void quill_i_count_avx2(const unsigned char *s, size_t nb, quill_i_tally *t)
{
    const __m256i Q = _mm256_set1_epi8('"'), N = _mm256_set1_epi8('\n');
    for (; nb; nb--, s += 64) {
        __m256i a = _mm256_loadu_si256(QUILL_I_PTR(const __m256i *, s));
        __m256i b = _mm256_loadu_si256(QUILL_I_PTR(const __m256i *, s + 32));
        uint64_t q = quill_i_mask_avx2(a, b, Q);
        quill_i_tally_block(t, q ? quill_i_clmul(q) : 0, quill_i_mask_avx2(a, b, N));
    }
}

static void quill_i_cpuid(unsigned leaf, unsigned r[4])
{
#if defined(_MSC_VER) && !defined(__clang__)
    int t[4];
    __cpuidex(t, QUILL_I_CAST(int, leaf), 0);
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
    return QUILL_I_CAST(uint64_t, hi) << 32 | lo;
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

#ifdef QUILL_I_NEON
/* NEON has no movemask, weight each lane by its bit and fold with pairwise adds */
QUILL_I_INLINE uint64_t quill_i_mask_neon(const uint8x16_t *v, uint8x16_t c, uint8x16_t w)
{
    uint8x16_t a = vpaddq_u8(vandq_u8(vceqq_u8(v[0], c), w), vandq_u8(vceqq_u8(v[1], c), w));
    uint8x16_t b = vpaddq_u8(vandq_u8(vceqq_u8(v[2], c), w), vandq_u8(vceqq_u8(v[3], c), w));
    a = vpaddq_u8(a, b);
    return vgetq_lane_u64(vreinterpretq_u64_u8(vpaddq_u8(a, a)), 0);
}

QUILL_I_INLINE size_t quill_i_neon(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid,
                                   const int sparse)
{
    static const unsigned char weights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    quill_i_state t = p->st;
    uint32_t *o = p->idx + p->n, *lim = p->idx + QUILL_I_CAP - 64;
    const uint8x16_t W = vld1q_u8(weights), Q = vdupq_n_u8('"'), D = vdupq_n_u8(p->delim);
    const uint8x16_t N = vdupq_n_u8('\n'), R = vdupq_n_u8('\r');
    size_t k;
    for (k = 0; k < nb && o <= lim; k++, s += 64, off += 64) {
        uint8x16_t v[4];
        uint64_t q;
        v[0] = vld1q_u8(s);
        v[1] = vld1q_u8(s + 16);
        v[2] = vld1q_u8(s + 32);
        v[3] = vld1q_u8(s + 48);
        if (sparse) {
            uint8x16_t any = vdupq_n_u8(0);
            int i;
            for (i = 0; i < 4; i++)
                any = vorrq_u8(any, vorrq_u8(vceqq_u8(v[i], Q), vorrq_u8(vceqq_u8(v[i], D), vceqq_u8(v[i], N))));
            if (!vmaxvq_u8(any))
                continue;
        }
        q = quill_i_mask_neon(v, Q, W);
#if defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
        uint64_t pq = vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64(q, ~0ULL)), 0);
#else
        uint64_t pq = quill_i_pxor(q);
#endif
        if (quill_i_block(&t, &o, off, q, pq, quill_i_mask_neon(v, D, W), quill_i_mask_neon(v, N, W),
                          t.strict ? quill_i_mask_neon(v, R, W) : 0, valid)) {
            k++;
            break;
        }
    }
    p->st = t;
    p->n = QUILL_I_CAST(uint32_t, o - p->idx);
    return k;
}

static size_t quill_i_run_neon(quill_parser *p, const unsigned char *s, size_t nb, uint32_t off, uint64_t valid)
{
    return p->sparse && !p->st.strict ? quill_i_neon(p, s, nb, off, valid, 1) : quill_i_neon(p, s, nb, off, valid, 0);
}

static void quill_i_count_neon(const unsigned char *s, size_t nb, quill_i_tally *t)
{
    static const unsigned char weights[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
    const uint8x16_t W = vld1q_u8(weights), Q = vdupq_n_u8('"'), N = vdupq_n_u8('\n');
    for (; nb; nb--, s += 64) {
        uint8x16_t v[4];
        v[0] = vld1q_u8(s);
        v[1] = vld1q_u8(s + 16);
        v[2] = vld1q_u8(s + 32);
        v[3] = vld1q_u8(s + 48);
        uint64_t q = quill_i_mask_neon(v, Q, W);
#if defined(__ARM_FEATURE_AES) || defined(__ARM_FEATURE_CRYPTO)
        uint64_t pq = q ? vgetq_lane_u64(vreinterpretq_u64_p128(vmull_p64(q, ~0ULL)), 0) : 0;
#else
        uint64_t pq = q ? quill_i_pxor(q) : 0;
#endif
        quill_i_tally_block(t, pq, quill_i_mask_neon(v, N, W));
    }
}
#endif

static void quill_i_count_bytes(const unsigned char *s, size_t n, quill_i_tally *t)
{
    const unsigned char *e = s + n;
    for (; s < e; s++) {
        if (*s == '"') {
            t->inq = ~t->inq;
        } else if (*s == '\n') {
            t->lines++;
            t->rows += QUILL_I_CAST(uint64_t, !t->inq);
        }
    }
}

static void quill_i_count_scalar(const unsigned char *s, size_t nb, quill_i_tally *t)
{
    quill_i_count_bytes(s, 64 * nb, t);
}

typedef size_t (*quill_i_run_fn)(quill_parser *, const unsigned char *, size_t, uint32_t, uint64_t);
typedef void (*quill_i_count_fn)(const unsigned char *, size_t, quill_i_tally *);

static const struct {
    const char *name;
    quill_i_run_fn run;
    quill_i_count_fn count;
    int level;
} quill_i_kernels[] = {
    {"scalar", quill_i_run_scalar, quill_i_count_scalar, 0},
#ifdef QUILL_I_X64
    {"sse2", quill_i_run_sse2, quill_i_count_sse2, 1},
    {"avx2", quill_i_run_avx2, quill_i_count_avx2, 2},
#elif defined(QUILL_I_NEON)
    {"neon", quill_i_run_neon, quill_i_count_neon, 1},
#endif
};

static int quill_i_level(void)
{
#ifdef QUILL_I_X64
#if defined(__GNUC__) || defined(__clang__)
    static int cached;
    int l = __atomic_load_n(&cached, __ATOMIC_RELAXED);
    if (!l) {
        l = quill_i_detect();
        __atomic_store_n(&cached, l, __ATOMIC_RELAXED);
    }
#else
    static volatile int cached;
    int l = cached;
    if (!l) {
        l = quill_i_detect();
        cached = l;
    }
#endif
    return l;
#elif defined(QUILL_I_NEON)
    return 1;
#else
    return 0;
#endif
}

static int quill_i_best(void)
{
    int l = quill_i_level(), k = 0;
    while (k + 1 < QUILL_I_CAST(int, sizeof quill_i_kernels / sizeof quill_i_kernels[0])
           && quill_i_kernels[k + 1].level <= l)
        k++;
    return k;
}

int quill_init(quill_parser *p, char delim, int flags)
{
    unsigned char c = QUILL_I_CAST(unsigned char, delim);
    if (c == '"' || c == '\n' || c == '\r' || c == 0)
        return -1;
    memset(p, 0, offsetof(quill_parser, idx));
    p->run = quill_i_kernels[quill_i_best()].run;
    p->delim = c;
    p->row_start = 1;
    p->stop = QUILL_I_CAST(size_t, -1);
    p->st.sep = 1;
    p->st.strict = (flags & QUILL_STRICT) != 0;
    return 0;
}

void quill_feed(quill_parser *p, const char *buf, size_t len, int final)
{
    size_t keep = p->buf ? QUILL_I_CAST(size_t, p->scan - p->field) : 0;
    if (p->buf)
        p->off0 += QUILL_I_CAST(uint64_t, p->field - p->buf);
    p->buf = p->field = buf;
    p->scan = buf + keep;
    p->end = buf + len;
    p->final = QUILL_I_CAST(unsigned char, final != 0);
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
    const unsigned char *s = QUILL_I_PTR(const unsigned char *, p->scan),
                        *end = QUILL_I_PTR(const unsigned char *, p->end);
    size_t nb = QUILL_I_CAST(size_t, end - s) / 64, k;
    if (nb > QUILL_I_BLOCKS)
        nb = QUILL_I_BLOCKS;
    p->base = QUILL_I_PTR(const char *, s);
    p->n = 0;
    p->ip = p->ie = p->idx;
    p->st.gbase = p->off0 + QUILL_I_CAST(uint64_t, QUILL_I_PTR(const char *, s) - p->buf);
    k = p->run(p, s, nb, 0, ~QUILL_I_CAST(uint64_t, 0));
    p->sparse = p->n < 2 * k;
    s += 64 * k;
    if (k == nb && p->final && !p->st.err && s < end && QUILL_I_CAST(size_t, end - s) < 64) {
        unsigned char pad[64] = {0};
        size_t rem = QUILL_I_CAST(size_t, end - s);
        memcpy(pad, s, rem);
        if (p->run(p, pad, 1, QUILL_I_CAST(uint32_t, s - QUILL_I_PTR(const unsigned char *, p->base)),
                   (QUILL_I_CAST(uint64_t, 1) << rem) - 1))
            s = end;
    }
    p->scan = QUILL_I_PTR(const char *, s);
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
    return p->field > p->origin ? QUILL_I_CAST(unsigned char, p->field[-1]) != p->delim : p->row_start;
}

static int quill_i_slow(quill_parser *p, quill_field *f)
{
    if (p->halt)
        return QUILL_END;
    if (p->skip) {
        int rc;
        p->skip = 0;
        while ((rc = quill_next(p, f)) == QUILL_FIELD && !f->eol) {
        }
        return rc == QUILL_FIELD ? quill_next(p, f) : rc;
    }
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
            quill_i_emit(p, f, e, QUILL_I_CAST(unsigned char, *e) != p->delim, 0);
            return QUILL_FIELD;
        }
        if (p->st.err)
            return quill_i_fail(p, p->st.err_at, p->st.err);
    }
    if (!p->final) {
        p->row_start = QUILL_I_CAST(unsigned char, quill_i_row_start(p));
        return QUILL_MORE;
    }
    if (p->done)
        return QUILL_END;
    p->done = 1;
    if (p->st.strict) {
        uint64_t at = p->off0 + QUILL_I_CAST(uint64_t, p->end - p->buf);
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

static void quill_i_count(quill_i_count_fn count, const char *buf, quill_range *r)
{
    const unsigned char *s = QUILL_I_PTR(const unsigned char *, buf) + r->start;
    size_t len = r->end > r->start ? r->end - r->start : 0;
    quill_i_tally t = {0, 0, 0};
    count(s, len / 64, &t);
    quill_i_count_bytes(s + len / 64 * 64, len % 64, &t);
    r->odd_quotes = QUILL_I_CAST(int, t.inq & 1);
    r->newlines = t.lines;
    r->rows = t.rows;
}

void quill_count(const char *buf, quill_range *r)
{
    quill_i_count(quill_i_kernels[quill_i_best()].count, buf, r);
}

static size_t quill_i_data_start(const char *buf, size_t len)
{
    return len >= 3 && memcmp(buf, "\xEF\xBB\xBF", 3) == 0 ? 3 : 0;
}

uint64_t quill_count_rows(const char *buf, size_t len, const quill_range *ranges, size_t n)
{
    quill_range whole;
    uint64_t rows = 0;
    size_t j;
    int inq = 0;
    if (!n) {
        whole.start = 0;
        whole.end = len;
        quill_count(buf, &whole);
        ranges = &whole;
        n = 1;
    }
    for (j = 0; j < n; j++) {
        rows += inq ? ranges[j].newlines - ranges[j].rows : ranges[j].rows;
        inq ^= ranges[j].odd_quotes & 1;
    }
    return rows + QUILL_I_CAST(uint64_t, len > quill_i_data_start(buf, len) && (buf[len - 1] != '\n' || inq));
}

/* Starts p at range r given the quote state inq just before it */
static void quill_i_start_range(quill_parser *p, const char *buf, size_t len, const quill_range *r, int inq)
{
    size_t data = quill_i_data_start(buf, len);
    size_t start = r->start < data ? data : r->start, end = r->end < data ? data : r->end;
    unsigned char c;
    p->buf = buf;
    p->field = p->scan = p->origin = buf + start;
    p->end = buf + len;
    p->final = 1;
    p->stop = end;
    p->line0 = start;
    if (start == end) {
        p->halt = 1;
        return;
    }
    if (start == data)
        return;
    c = QUILL_I_CAST(unsigned char, buf[start - 1]);
    p->st.inq = 0 - QUILL_I_CAST(uint64_t, inq);
    p->st.sep = !inq && (c == p->delim || c == '\n');
    p->st.close = !inq && c == '"';
    p->st.cr = !inq && c == '\r';
    p->skip = inq || c != '\n';
}

int quill_init_range(quill_parser *p, char delim, int flags, const char *buf, size_t len, const quill_range *ranges,
                     size_t k)
{
    const quill_range *r = ranges + k;
    size_t j;
    int inq = 0;
    if (r->start > r->end || r->end > len || quill_init(p, delim, flags))
        return -1;
    for (j = 0; j < k; j++) {
        inq ^= ranges[j].odd_quotes & 1;
        p->st.lines += ranges[j].newlines;
    }
    quill_i_start_range(p, buf, len, r, inq);
    return 0;
}

static int quill_i_guess(const char *buf, size_t len, size_t start, unsigned char delim)
{
    const char *s = buf + start, *lim = buf + (len - start > 65536 ? start + 65536 : len), *q;
    int parity = 0;
    while ((q = QUILL_I_CAST(const char *, memchr(s, '"', QUILL_I_CAST(size_t, lim - s)))) != QUILL_I_NULL) {
        unsigned char next = q + 1 < buf + len ? QUILL_I_CAST(unsigned char, q[1]) : '\n';
        unsigned char prev = QUILL_I_CAST(unsigned char, q[-1]);
        if (next != '"' && next != delim && next != '\n' && next != '\r')
            return parity;
        if (prev != '"' && prev != delim && prev != '\n' && prev != '\r')
            return !parity;
        parity = !parity;
        s = q + 1;
    }
    return 0;
}

int quill_init_guess(quill_parser *p, char delim, int flags, const char *buf, size_t len, quill_range *ranges, size_t k)
{
    quill_range *r = ranges + k;
    size_t data = quill_i_data_start(buf, len);
    if (r->start > r->end || r->end > len || (flags & QUILL_STRICT) || quill_init(p, delim, flags))
        return -1;
    r->guess = r->start <= data || r->start == r->end ? 0 : quill_i_guess(buf, len, r->start, p->delim);
    r->odd_quotes = 0;
    quill_i_start_range(p, buf, len, r, r->guess);
    p->guessed = r;
    p->st.guard = 1;
    return 0;
}

int quill_guess_ok(const quill_range *ranges, size_t k)
{
    size_t j;
    int inq = 0;
    for (j = 0; j < k; j++)
        inq ^= ranges[j].odd_quotes & 1;
    return inq == ranges[k].guess;
}

static void quill_i_finish_guess(quill_parser *p)
{
    quill_range *r = p->guessed, tail;
    size_t data = quill_i_data_start(p->buf, QUILL_I_CAST(size_t, p->end - p->buf));
    size_t end = r->end < data ? data : r->end, scan = QUILL_I_CAST(size_t, p->scan - p->buf);
    p->guessed = QUILL_I_NULL;
    if (r->start == r->end)
        return;
    tail.start = scan < end ? scan : end;
    tail.end = scan > end ? scan : end;
    quill_count(p->buf, &tail);
    r->odd_quotes = QUILL_I_CAST(int, p->st.inq & 1) ^ r->guess ^ tail.odd_quotes;
}

int quill_i_next_slow(quill_parser *p, quill_field *f)
{
    int rc = quill_i_slow(p, f);
    if (rc != QUILL_FIELD && p->guessed) {
        quill_range *r = p->guessed;
        quill_i_finish_guess(p);
        if (rc == QUILL_ERROR) {
            r->guess = -1;
            p->err_msg = QUILL_I_NULL;
            p->halt = 1;
            rc = QUILL_END;
        }
    }
    return rc;
}

const char *quill_i_unescape(const quill_field *f, char *dst, size_t *len)
{
    const char *s = f->ptr, *e = f->ptr + f->len, *q;
    char *d = dst;
    int in = 1;
    while ((q = QUILL_I_CAST(const char *, memchr(s, '"', QUILL_I_CAST(size_t, e - s)))) != QUILL_I_NULL) {
        memcpy(d, s, QUILL_I_CAST(size_t, q - s));
        d += q - s;
        if (in && q + 1 < e && q[1] == '"') {
            *d++ = '"';
            s = q + 2;
        } else {
            in = !in;
            s = q + 1;
        }
    }
    memcpy(d, s, QUILL_I_CAST(size_t, e - s));
    *len = QUILL_I_CAST(size_t, d - dst) + QUILL_I_CAST(size_t, e - s);
    return dst;
}

#endif
#endif
