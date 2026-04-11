import csv
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV_DIR = os.path.join(HERE, "csv")
EXE = ".exe" if os.name == "nt" else ""
PY_LIMIT = 16 << 20
SMALL_MODES = ["whole", "stream:1", "stream:7", "stream:64", "stream:4096", "ranges:2", "ranges:5"]
BIG_MODES = ["whole", "stream:65536", "ranges:8"]


NTSTATUS = {
    0xC0000135: "a DLL was not found, for the sanitizer runtime put `clang -print-runtime-dir` on PATH",
    0xC0000005: "access violation",
    0xC00000FD: "stack overflow",
    0xC0000409: "fast fail, a stack buffer overrun or abort",
}


def tool(build, name):
    return os.path.join(build, name + EXE)


def status(rc):
    if os.name == "nt" and rc & 0xC0000000 == 0xC0000000:
        code = rc & 0xFFFFFFFF
        return f"status 0x{code:08X}" + (f", {NTSTATUS[code]}" if code in NTSTATUS else "")
    return f"signal {-rc}" if rc < 0 else f"exit code {rc}"


def launch(args, capture=True):
    p = subprocess.run(args, capture_output=capture)
    if p.returncode:
        err = p.stderr.decode(errors="replace").strip() if capture else ""
        sys.exit(f"FAIL: {os.path.basename(args[0])} {status(p.returncode)}" + (f"\n{err}" if err else ""))
    return p.stdout.decode(errors="replace") if capture else ""


def dump(build, path, delim, kernel=None, mode="whole", strict=False, checksum=False):
    args = [tool(build, "dump"), "-d", "tab" if delim == "\t" else delim, "-m", mode]
    args += ["-k", kernel] if kernel else []
    args += ["-s"] if strict else []
    args += ["-c"] if checksum else []
    out = launch(args + [path])
    rows, error = [], None
    for line in out.splitlines():
        if line.startswith("!error "):
            ln, col, msg = line[7:].split(" ", 2)
            error = [int(ln), int(col), msg]
        elif checksum:
            rows.append(line)
        else:
            rows.append(json.loads(line))
    return rows, error


def python_rows(path, delim):
    csv.field_size_limit(min(sys.maxsize, 2**31 - 1))
    with open(path, newline="", encoding="latin-1") as f:
        if f.read(3) != "\xef\xbb\xbf":
            f.seek(0)
        return [row for row in csv.reader(f, delimiter=delim)]


def delim_for(path):
    return "\t" if path.endswith(".tsv") else ","


def check_expected(build, kernels, path, failures):
    with open(path[:-4] + ".json", encoding="utf-8") as f:
        exp = json.load(f)
    delim = delim_for(path)
    want = [[cell.encode("utf-8").decode("latin-1") for cell in row] for row in exp["rows"]]
    want_err = exp["strict_error"]
    for kernel in kernels:
        for mode in SMALL_MODES:
            rows, err = dump(build, path, delim, kernel, mode)
            if rows != want or err:
                failures.append(f"{os.path.basename(path)} lenient {kernel} {mode}: got {rows} {err}")
            rows, err = dump(build, path, delim, kernel, mode, strict=True)
            if want_err is None and (rows != want or err):
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: got {rows} {err}")
            elif want_err is not None and err != want_err:
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: error {err}, want {want_err}")
    return len(kernels) * len(SMALL_MODES) * 2


def check_generated(build, kernels, path, failures):
    delim = delim_for(path)
    name = os.path.basename(path)
    runs = 0
    if os.path.getsize(path) <= PY_LIMIT:
        rows, err = dump(build, path, delim)
        runs += 1
        if err or rows != python_rows(path, delim):
            failures.append(f"{name}: differs from Python's csv module")
    base, _ = dump(build, path, delim, checksum=True)
    for kernel in kernels:
        for mode in BIG_MODES:
            for strict in (False, True):
                got, err = dump(build, path, delim, kernel, mode, strict, checksum=True)
                runs += 1
                if got != base or err:
                    failures.append(f"{name} {kernel} {mode} {'strict' if strict else 'lenient'}: {got} {err}")
    return runs


def csv_files():
    files = glob.glob(os.path.join(CSV_DIR, "*.csv")) + glob.glob(os.path.join(CSV_DIR, "*.tsv"))
    return sorted(files, key=os.path.getsize)


def test(build):
    print("differential suite", flush=True)
    launch([tool(build, "differential")], capture=False)
    kernels = launch([tool(build, "dump"), "-l"]).split()
    print(f"kernels: {' '.join(kernels)}")
    failures, runs = [], 0
    for path in csv_files():
        expected = os.path.exists(path[:-4] + ".json")
        before = len(failures)
        runs += (check_expected if expected else check_generated)(build, kernels, path, failures)
        status = "ok" if len(failures) == before else "FAIL"
        print(f"  {status:4} {os.path.basename(path):34} {human(os.path.getsize(path)):>9}", flush=True)
    for f in failures[:50]:
        print("FAIL:", f)
    print(f"{runs} parses, {len(failures)} failures")
    return 1 if failures else 0


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024


def bench(build):
    files = csv_files()
    if not any(os.path.basename(f).startswith("gen_") for f in files):
        print("no generated files in tests/csv yet, `just gen` makes the default set\n")
    header = None
    for path in files:
        out = launch([tool(build, "bench"), path, "-d", "tab" if path.endswith(".tsv") else ","]).splitlines()
        kernels, strict, best_time = {}, {}, None
        for line in out:
            f = line.split()
            if f[0] == "kernel":
                gbps, us = float(f[3]), float(f[5])
                (kernels if f[2] == "lenient" else strict)[f[1]] = gbps
                if f[2] == "lenient":
                    best_time = us
        if header is None:
            names = list(kernels)
            header = f"{'file':34} {'size':>9} " + " ".join(f"{n:>8}" for n in names)
            header += f" {'strict':>8} {'time':>10}"
            print("GB/s, lenient per kernel, strict and time on the best kernel")
            print(header)
            print("-" * len(header))
        best = names[-1]
        t = f"{best_time:.1f} us" if best_time < 1000 else f"{best_time / 1000:.1f} ms"
        print(f"{os.path.basename(path):34} {human(os.path.getsize(path)):>9} "
              + " ".join(f"{kernels[n]:8.2f}" for n in names)
              + f" {strict[best]:8.2f} {t:>10}", flush=True)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("test", "bench"):
        print(__doc__)
        sys.exit(2)
    sys.exit((test if sys.argv[1] == "test" else bench)(sys.argv[2]))
