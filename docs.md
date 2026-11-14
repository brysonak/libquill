# quill documentation

- [Setup](#setup)
- [Parsing a buffer](#parsing-a-buffer)
- [Fields and values](#fields-and-values)
- [Streaming](#streaming)
- [Errors and strict mode](#errors-and-strict-mode)
- [How input is read](#how-input-is-read)
- [Multithreading](#multithreading)
- [Counting rows](#counting-rows)
- [API reference](#api-reference)
- [How it works](#how-it-works)

## Setup

quill is one header, `include/quill.h`. It needs only the C standard library and builds as
C99 or later, or C++11 or later.

In exactly one source file, define `QUILL_IMPLEMENTATION` before the include. That file
compiles the function bodies. Every other file includes the header normally.

```c
#define QUILL_IMPLEMENTATION
#include "quill.h"
```

No compiler flags are needed for SIMD. On x86-64 the AVX2 code is compiled with per-function
target attributes and chosen at runtime. On ARM64, NEON is always used, and the carry-less
multiply (PMULL) is used when the compiler targets the crypto extension
(`-march=armv8-a+crypto`, the default on Apple Silicon).

## Parsing a buffer

```c
static quill_parser p;
quill_field f;
int rc;

quill_init(&p, ',', 0);
quill_feed(&p, buf, len, 1);
while ((rc = quill_next(&p, &f)) == QUILL_FIELD) {
    /* use f */
}
/* rc is QUILL_END, or QUILL_ERROR in strict mode */
```

`quill_parser` holds a 16 KB index buffer, so declare it `static` or allocate it rather than
putting it on a small stack. One parser handles one input at a time and is not shared between
threads. Parsers in different threads are independent.

`quill_next` returns one field at a time, in order. `f.eol` is set on the last field of each
row, so a row ends at every field with `eol` set.

## Fields and values

```c
typedef struct {
    const char *ptr;  /* raw bytes, outer quotes removed */
    size_t len;
    int quoted;       /* may hold "" escapes */
    int eol;
} quill_field;
```

`ptr` points into the buffer you passed to `quill_feed`. Nothing is copied. For a quoted field
the surrounding quotes are already stripped, but any `""` escapes inside are still there, and
`quoted` is set.

To get the real value, call `quill_value`:

```c
char tmp[256];  /* must hold at least f.len bytes */
size_t n;
const char *v = quill_value(&f, tmp, &n);
```

If the field needs no unescaping, `v` is `f.ptr` and `n` is `f.len`, with no copy. Otherwise
the unescaped value is written to `tmp` and `v` points there. Unescaping only removes bytes,
so `f.len` bytes is always enough room.

If you only need to compare a field against a string, you can skip `quill_value` for
unquoted fields and compare `f.ptr` directly.

## Streaming

The input doesn't have to be in memory all at once. Pass `final = 0` while more input is
coming. When the parser has handed out every complete field in the buffer, `quill_next`
returns `QUILL_MORE`. `quill_consumed` then says how many bytes from the start of the buffer
are finished. Keep the rest at the front, append more input, and feed the whole buffer again.

```c
static quill_parser p;
quill_field f;
char buf[1 << 16];
size_t have = 0, want, got;
int rc, final;

quill_init(&p, ',', 0);
do {
    want = sizeof buf - have;
    got = fread(buf + have, 1, want, in);
    have += got;
    final = got < want;
    quill_feed(&p, buf, have, final);
    while ((rc = quill_next(&p, &f)) == QUILL_FIELD) {
        /* use f */
    }
    have -= quill_consumed(&p);
    memmove(buf, buf + quill_consumed(&p), have);
} while (rc == QUILL_MORE);
```

Fields point into `buf`, so they are only valid until you move or overwrite those bytes.
Copy anything you need to keep before the `memmove`.

The buffer must be bigger than the longest row. If a row can be longer, grow the buffer
whenever it fills up (`have == sizeof buf`) before reading more, like `tests/dump.c` does with
`realloc`.

## Errors and strict mode

By default quill never fails. It reads malformed input the way most CSV tools do, described
in [How input is read](#how-input-is-read).

With `QUILL_STRICT`, the first problem stops the parse. `quill_next` returns `QUILL_ERROR`,
and the parser holds:

| Field | Meaning |
|---|---|
| `p.err_line` | 1-based line of the problem |
| `p.err_col` | 1-based byte column of the offending byte |
| `p.err_offset` | 0-based byte offset from the start of the input |
| `p.err_msg` | one of the messages below |

| Message | Example |
|---|---|
| `quote inside unquoted field` | `a,b"c` |
| `unexpected byte after closing quote` | `"a"b,c` |
| `carriage return not followed by newline` | a bare `\r` outside quotes |
| `unterminated quoted field` | input ends inside quotes |

Fields returned before the error are valid. Every later call returns `QUILL_ERROR` again.

## How input is read

- Fields are separated by the delimiter given to `quill_init`, any single byte except `"`,
  `\r`, `\n` and NUL.
- Rows end at `\n` or `\r\n`. A bare `\r` is part of the field in lenient mode.
- A field starting with `"` is quoted. Inside it, delimiters and newlines are data, and `""`
  is a literal quote.
- A UTF-8 byte order mark at the very start is skipped.
- Whitespace is kept exactly: ` a , b ` gives `" a "` and `" b "`.
- An empty line is a row with one empty field. Empty input gives no rows.
- A missing final newline is fine: the last row still ends.
- Rows may have different numbers of fields. quill doesn't check.
- quill doesn't treat the first row as a header. It's just the first row.

Lenient mode follows quotes wherever they appear. A quote in the middle of an unquoted field
starts quoted text, and text after a closing quote is kept, so `"a"b` reads as `ab`. That is
what most tools do, but it means one stray quote can swallow the rest of a file. Use
`QUILL_STRICT` when you would rather be told.

## Multithreading

quill doesn't create threads. Instead it can parse one byte range of a buffer on its own, so
you can split a file across your own threads. Each range returns the rows that start inside
it. The first row a range touches belongs to the previous range, and the last row may run past
the range's end, so every row is returned exactly once.

A range can't tell by itself whether its first byte is inside a quoted field. There are two
ways to find out.

### Exact

Every range first counts its quotes with `quill_count`, which is a fast pass that only looks
for `"` and `\n`. With those counts, `quill_init_range` knows each range's start state for
certain.

```c
quill_range r[N];
for (k = 0; k < N; k++) {
    r[k].start = len / N * k;
    r[k].end = k + 1 == N ? len : len / N * (k + 1);
}

/* in thread k */
quill_count(buf, &r[k]);

/* after all counts are done, in thread k */
quill_init_range(&p[k], ',', 0, buf, len, r, k);
while (quill_next(&p[k], &f) == QUILL_FIELD) {
    /* use f */
}
```

Exact mode works with `QUILL_STRICT`, and line numbers in errors are right.

### Guessed

Guessed mode skips the counting pass. `quill_init_guess` looks at the quotes near the start of
the range and guesses the state, and the thread starts parsing at once. After every range has
been parsed to `QUILL_END`, `quill_guess_ok` checks each guess against the real quote counts.
A range that guessed wrong is parsed again with `quill_init_range`, which is exact.

```c
/* in thread k */
quill_init_guess(&p[k], ',', 0, buf, len, r, k);
while (quill_next(&p[k], &f) == QUILL_FIELD) {
    /* collect results for range k */
}

/* after every thread has finished */
for (k = 0; k < N; k++) {
    if (!quill_guess_ok(r, k)) {
        /* drop range k's results and parse it again */
        quill_init_range(&p[k], ',', 0, buf, len, r, k);
        while (quill_next(&p[k], &f) == QUILL_FIELD) {
            /* collect results for range k */
        }
    }
}
```

A wrong guess is always caught, so results are always correct. For valid CSV the guess is
wrong only when the 64 KB after a range's start has no quote that settles it, which in
practice means quoted fields longer than that. Input with stray quotes in unquoted fields
can also fool it.

A wrong guess is usually caught early, too. While parsing a guessed range, quill watches for
an opening quote right after an ordinary byte, which valid CSV never contains, so under a
correct guess it never fires. The first time it fires, the parse stops and returns
`QUILL_END`, and `quill_guess_ok` reports the range as wrong. A bad guess then costs a short
false start plus the exact re-parse, instead of a full parse of garbage. The check only looks
at quotes, so it costs nothing measurable on normal files.

Because the parse itself can withdraw a guess, call `quill_guess_ok(r, k)` only after range `k`
has been parsed to `QUILL_END`, not before. Re-parse failed ranges in parallel when you can:
every range's quote count is exact by then, so they no longer depend on each other.

Guessed mode doesn't support `QUILL_STRICT`: `quill_init_guess` returns -1.

`benchmarks/quill/bench.c` is a complete threaded example using guessed mode.

## Counting rows

To count rows without parsing fields, use `quill_count_rows`. It only looks for quotes and
newlines, so it runs several times faster than a `quill_next` loop. It counts exactly the rows
a parse would return: newlines inside quoted fields don't end rows, and a last row without a
newline still counts.

```c
uint64_t rows = quill_count_rows(buf, len, NULL, 0);
```

Row counting also works in parallel, and needs no guessing. Each thread calls `quill_count` on
its own range, then one call combines them:

```c
/* in thread k */
quill_count(buf, &r[k]);

/* after all counts are done */
uint64_t rows = quill_count_rows(buf, len, r, N);
```

`quill_count` records newline counts for both possible start states, so the combine step can
pick the right one for each range once the quote parity before it is known.

## API reference

### Constants

```c
enum { QUILL_ERROR = -1, QUILL_END = 0, QUILL_FIELD = 1, QUILL_MORE = 2 };
enum { QUILL_STRICT = 1 };
```

### `int quill_init(quill_parser *p, char delim, int flags)`

Resets `p` for a new input. `flags` is 0 or `QUILL_STRICT`. Returns 0, or -1 if `delim` is
`"`, `\r`, `\n` or NUL.

### `void quill_feed(quill_parser *p, const char *buf, size_t len, int final)`

Hands the parser `len` bytes at `buf`. Pass `final = 1` when this is the end of the input.
When streaming, `buf` must start with the bytes `quill_consumed` left unfinished, followed by
the new input. `buf` must stay valid and unchanged while you read fields from it.

### `int quill_next(quill_parser *p, quill_field *f)`

Stores the next field in `f` and returns `QUILL_FIELD`. Otherwise returns `QUILL_MORE` (feed
more input), `QUILL_END` (input finished) or `QUILL_ERROR` (strict mode only). Inline.

### `size_t quill_consumed(const quill_parser *p)`

After `QUILL_MORE`, the number of bytes at the start of the current buffer that are finished
and can be dropped. Inline.

### `const char *quill_value(const quill_field *f, char *dst, size_t *len)`

Returns the unescaped value of `f` and stores its length in `*len`. Points into the input
when no unescaping is needed, otherwise into `dst`, which must hold at least `f->len` bytes.
Inline.

### `quill_range`

```c
typedef struct {
    size_t start, end;  /* set by the caller */
    int odd_quotes;
    int guess;
    uint64_t newlines, rows;
} quill_range;
```

Set `start` and `end` for every range so they cover `[0, len)` in order without gaps. The
other fields are filled in by quill.

### `void quill_count(const char *buf, quill_range *r)`

Counts quotes and newlines in `buf[r->start .. r->end)`, including the newlines that would end
rows for either start state. Safe to call for every range at once from different threads.

### `uint64_t quill_count_rows(const char *buf, size_t len, const quill_range *ranges, size_t n)`

Returns the number of rows in `buf`. With `n = 0` it counts the whole buffer on the calling
thread and `ranges` is ignored. Otherwise `ranges` must hold `n` ranges covering `[0, len)`,
each filled in by `quill_count`.

### `int quill_init_range(quill_parser *p, char delim, int flags, const char *buf, size_t len, const quill_range *ranges, size_t k)`

Sets `p` up to parse range `k` of `buf`. Needs exact `odd_quotes` for ranges `0` to `k - 1`,
from `quill_count` or from a guessed parse that ran to `QUILL_END`. Returns 0, or -1 on bad
arguments.

### `int quill_init_guess(quill_parser *p, char delim, int flags, const char *buf, size_t len, quill_range *ranges, size_t k)`

Sets `p` up to parse range `k` with a guessed start state, recorded in `ranges[k].guess`. When
the parse reaches `QUILL_END`, `ranges[k].odd_quotes` is filled in, and `ranges[k].guess` is
cleared if the parse found the guess was wrong. Writes only `ranges[k]`, so each thread can own
its own entry. Returns 0, or -1 on bad arguments or with `QUILL_STRICT`.

### `int quill_guess_ok(const quill_range *ranges, size_t k)`

Returns nonzero if range `k` guessed right. Needs every range up to `k` parsed to `QUILL_END`
first.

### Internal names

Anything prefixed `quill_i_` or `QUILL_I_` is internal and may change.

## How it works

Parsing happens in two stages.

**Stage 1** reads 64 bytes at a time. SIMD compares give a 64-bit mask each for quotes, the
delimiter, `\n` and `\r`, one bit per byte. A prefix XOR of the quote mask marks which bytes are
inside quotes. It's done with a single carry-less multiply (PCLMUL or PMULL) where the CPU has
one. One bit carries over to the next block, so quoted text can span blocks. Field ends are the
delimiters and newlines outside quotes. Those bits are turned into byte offsets in a fixed
4096-entry array.

**Stage 2** is `quill_next`, inlined into your loop. It takes the next offset, works out the
field's start, length, quoting and end of row, and returns. When the offsets run out, it runs
stage 1 on the next blocks.

When many blocks in a row contain no field ends, as in long text fields, stage 1 switches to a
mode that skips empty blocks.
