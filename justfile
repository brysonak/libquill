set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

cc := "clang"
cxx := "clang++"
py := if os_family() == "windows" { "python" } else { "python3" }
exe := if os_family() == "windows" { ".exe" } else { "" }
pthread := if os_family() == "windows" { "" } else { "-pthread" }
mkdir := if os_family() == "windows" { "New-Item -ItemType Directory -Force build | Out-Null" } else { "mkdir -p build" }
crt := if os_family() == "windows" { "-D_CRT_SECURE_NO_WARNINGS" } else { "" }
cxxstd := if os_family() == "windows" { "c++14" } else { "c++11" }
warn := "-Wall -Wextra -Wpedantic -Werror -Iinclude " + crt
xwarn := warn + " -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wold-style-cast -Wzero-as-null-pointer-constant"
san := '-O1 -g "-fsanitize=address,undefined" -fno-sanitize-recover=all'
bflags := ""

copy_asan := if os_family() == "windows" { "Copy-Item -Force '" + `clang "-print-file-name=clang_rt.asan_dynamic-x86_64.dll"` + "' build" } else { "true" }

test:
    {{mkdir}}
    {{copy_asan}}
    {{cc}} -std=c99 {{warn}} {{san}} tests/differential.c -o build/differential{{exe}}
    {{cc}} -std=c99 {{warn}} {{san}} tests/dump.c -o build/dump{{exe}}
    {{cxx}} -std={{cxxstd}} {{xwarn}} {{san}} tests/cxx.cpp -o build/cxx{{exe}}
    {{cxx}} -std={{cxxstd}} {{warn}} -Wno-missing-field-initializers {{san}} -x c++ tests/differential.c -o build/differential_cxx{{exe}}
    ./build/cxx{{exe}}
    ./build/differential_cxx{{exe}}
    {{py}} tests/run.py test build

bench:
    {{mkdir}}
    {{cc}} -std=c11 {{warn}} -O2 {{bflags}} {{pthread}} tests/bench.c -o build/bench{{exe}}
    {{py}} tests/run.py bench build

gen *args:
    {{py}} tests/gen_csv.py {{args}}

fuzz secs="60" *args:
    {{mkdir}}
    {{copy_asan}}
    {{py}} -c "import os; os.makedirs('build/corpus', exist_ok=True)"
    {{cc}} -std=c99 {{warn}} -O1 -g "-fsanitize=fuzzer,address,undefined" tests/fuzz/parse.c -o build/fuzz{{exe}}
    ./build/fuzz{{exe}} build/corpus tests/fuzz/seeds "-dict=tests/fuzz/csv.dict" -max_total_time={{secs}} -artifact_prefix=build/ {{args}}

fuzz-check:
    {{mkdir}}
    {{cc}} -std=c99 {{warn}} -O2 tests/dump.c -o build/dump{{exe}}
    {{py}} tests/fuzz/crosscheck.py build build/corpus

fmt:
    clang-format -i include/quill.h benchmarks/quill/bench.c benchmarks/zsv/bench.c benchmarks/cisv/bench.c tests/bench.c tests/differential.c tests/dump.c tests/cxx.cpp tests/fuzz/parse.c

compare *args:
    {{py}} benchmarks/run.py {{args}}
