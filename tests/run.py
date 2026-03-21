import csv
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV_DIR = os.path.join(HERE, "csv")
EXE = ".exe" if os.name == "nt" else ""
MODES = ["whole", "stream:1", "stream:7", "stream:64", "stream:4096"]


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


def dump(build, path, delim, kernel=None, mode="whole", strict=False):
    args = [tool(build, "dump"), "-d", "tab" if delim == "\t" else delim, "-m", mode]
    args += ["-k", kernel] if kernel else []
    args += ["-s"] if strict else []
    out = launch(args + [path])
    rows, error = [], None
    for line in out.splitlines():
        if line.startswith("!error "):
            ln, col, msg = line[7:].split(" ", 2)
            error = [int(ln), int(col), msg]
        else:
            rows.append(json.loads(line))
    return rows, error


def python_rows(path, delim):
    csv.field_size_limit(sys.maxsize)
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
        for mode in MODES:
            rows, err = dump(build, path, delim, kernel, mode)
            if rows != want or err:
                failures.append(f"{os.path.basename(path)} lenient {kernel} {mode}: got {rows} {err}")
            rows, err = dump(build, path, delim, kernel, mode, strict=True)
            if want_err is None and (rows != want or err):
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: got {rows} {err}")
            elif want_err is not None and err != want_err:
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: error {err}, want {want_err}")
    return len(kernels) * len(MODES) * 2


def check_generated(build, kernels, path, failures):
    delim = delim_for(path)
    rows, err = dump(build, path, delim)
    if err or rows != python_rows(path, delim):
        failures.append(f"{os.path.basename(path)}: differs from Python's csv module")
    return 1


def csv_files():
    files = glob.glob(os.path.join(CSV_DIR, "*.csv")) + glob.glob(os.path.join(CSV_DIR, "*.tsv"))
    return sorted(files)


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
        print(f"  {status:4} {os.path.basename(path)}")
    for f in failures:
        print("FAIL:", f)
    print(f"{runs} parses, {len(failures)} failures")
    return 1 if failures else 0


def bench(build):
    for path in csv_files():
        print(os.path.basename(path), flush=True)
        for line in launch([tool(build, "bench"), path]).splitlines():
            print("  " + line)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("test", "bench"):
        print(__doc__)
        sys.exit(2)
    sys.exit((test if sys.argv[1] == "test" else bench)(sys.argv[2]))
