set windows-shell := ["powershell.exe", "-NoLogo", "-Command"]

cc := "clang"
py := if os_family() == "windows" { "python" } else { "python3" }
exe := if os_family() == "windows" { ".exe" } else { "" }
mkdir := if os_family() == "windows" { "New-Item -ItemType Directory -Force build | Out-Null" } else { "mkdir -p build" }
crt := if os_family() == "windows" { "-D_CRT_SECURE_NO_WARNINGS" } else { "" }
warn := "-Wall -Wextra -Wpedantic -Werror -Iinclude " + crt
san := "-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all"

test:
    {{mkdir}}
    {{cc}} -std=c99 {{warn}} {{san}} tests/differential.c -o build/differential{{exe}}
    {{cc}} -std=c99 {{warn}} {{san}} tests/dump.c -o build/dump{{exe}}
    {{py}} tests/run.py test build

bench:
    {{mkdir}}
    {{cc}} -std=c11 {{warn}} -O2 tests/bench.c -o build/bench{{exe}}
    {{py}} tests/run.py bench build

gen *args:
    {{py}} tests/gen_csv.py {{args}}
