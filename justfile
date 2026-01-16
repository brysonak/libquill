cc := "clang"
warn := "-Wall -Wextra -Wpedantic -Werror -Iinclude"
san := "-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

test:
    mkdir -p build
    {{cc}} -std=c99 {{warn}} {{san}} tests/differential.c -o build/differential
    {{cc}} -std=c99 {{warn}} {{san}} tests/dump.c -o build/dump
    python3 tests/run.py test build

bench:
    mkdir -p build
    {{cc}} -std=c11 {{warn}} -O2 tests/bench.c -o build/bench
    python3 tests/run.py bench build
