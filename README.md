# quill

A fast CSV parser in a single C header. No dependencies beyond the C standard library,
builds as C99 or C++11, and uses SIMD (AVX2, SSE2 or NEON) to find field boundaries
64 bytes at a time.

```c
#define QUILL_IMPLEMENTATION
#include "quill.h"
#include <stdio.h>

int main(void)
{
    static const char csv[] = "name,quote\nBryson,\"says \"\"hi\"\"\"\n";
    static quill_parser p;
    quill_field f;
    char tmp[64];
    size_t n;
    quill_init(&p, ',', 0);
    quill_feed(&p, csv, sizeof csv - 1, 1);
    while (quill_next(&p, &f) == QUILL_FIELD) {
        const char *v = quill_value(&f, tmp, &n);
        printf("%.*s%s", (int)n, v, f.eol ? "\n" : " | ");
    }
    return 0;
}
```

```
name | quote
Bryson | says "hi"
```
## Using it

Copy `include/quill.h` into your project. In exactly one `.c` or `.cpp` file, define
`QUILL_IMPLEMENTATION` before including it. Everywhere else, include it normally.

The full API, with streaming and multithreading examples, is in [docs.md](docs.md).

## Platforms

| Architecture | Kernels |
|---|---|
| x86-64 | AVX2 + PCLMUL, SSE2 (picked at runtime) |
| ARM64 | NEON, with PMULL when built with the crypto extension |
| anything else | portable scalar |

Written for clang, GCC and MSVC. Tested with clang and GCC on Linux, clang on Windows, and on
ARM64 under QEMU.

## Benchmarks

`just compare` races quill against other parsers on `benchmarks/big.csv` (774 MB, 20 million
rows, 5 columns). Each parser runs as a whole process, so loading the file counts for
everyone, and every answer is checked against quill's. Results on a Ryzen 5 5500
(6 cores, 12 threads), best of 3, scanning every field and summing its unescaped length:

| Parser | Time | Throughput |
|---|---|---|
| quill, 12 threads | 85 ms | 9.11 GB/s |
| quill, 1 thread | 496 ms | 1.56 GB/s |
| cisv | 513 ms | 1.51 GB/s |
| duckdb | 654 ms | 1.18 GB/s |
| simd-csv (Rust) | 678 ms | 1.14 GB/s |
| polars | 866 ms | 0.89 GB/s |
| zsv | 868 ms | 0.89 GB/s |
| csv crate (Rust) | 1237 ms | 0.63 GB/s |
| pandas | 14008 ms | 0.06 GB/s |
| Python `csv` | 18670 ms | 0.04 GB/s |

For a file with realistic quoting (`""` escapes, CRLF inside quoted fields, Unicode, long text),
generate one and race on that too. The find name has spaces, so call run.py directly:

```
just gen 512M --profile mixed --out benchmarks/quoted.csv
python3 benchmarks/run.py -f benchmarks/quoted.csv -n 'he said "xray"'
```

`big.csv` is not in the repo. Put any large CSV at `benchmarks/big.csv`, or pass another
file with `just compare -f path/to.csv`. zsv and cisv are fetched at pinned commits into
`benchmarks/.deps`, and anything that can't be built (missing `cargo`, `make`, or a Python
module) is skipped with a reason. For polars, duckdb and pandas:

```
python -m venv benchmarks/venv
benchmarks/venv/bin/pip install polars duckdb pandas
source benchmarks/venv/bin/activate
```

## Development

Needs `clang`, [`just`](https://github.com/casey/just) and Python 3.

| Command | Does |
|---|---|
| `just test` | Differential tests against a reference parser across every kernel, chunk size and range split, edge-case CSVs with expected output, a C++ build, all under ASan and UBSan |
| `just gen` | Generates the large test files in `tests/csv` (dense, sparse, mixed, hostile) |
| `just bench` | Per-kernel throughput on the test files |
| `just fuzz [secs]` | libfuzzer run, 60 seconds by default |
| `just compare` | The benchmark above |
| `just fmt` | clang-format |

On Windows the recipes run in PowerShell, and `just test` copies clang's ASan runtime next to
the test binaries. On macOS, `just fuzz` needs Homebrew's `llvm`, since Apple's clang has no
libfuzzer.


# License
This project uses the MIT license, see [LICENSE](LICENSE).