cc := "clang"
warn := "-Wall -Wextra -Wpedantic -Werror -Iinclude"

test:
    mkdir -p build
    {{cc}} -std=c99 {{warn}} -O1 -g tests/dump.c -o build/dump
    python3 tests/run.py test build
