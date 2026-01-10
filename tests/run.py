import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV_DIR = os.path.join(HERE, "csv")
MODES = ["whole", "stream:1", "stream:7", "stream:64", "stream:4096"]


def dump(build, path, kernel=None, mode="whole", strict=False):
    args = [os.path.join(build, "dump"), "-m", mode]
    args += ["-k", kernel] if kernel else []
    args += ["-s"] if strict else []
    out = subprocess.run(args + [path], capture_output=True, check=True).stdout.decode()
    rows, error = [], None
    for line in out.splitlines():
        if line.startswith("!error "):
            ln, col, msg = line[7:].split(" ", 2)
            error = [int(ln), int(col), msg]
        else:
            rows.append(json.loads(line))
    return rows, error


def check(build, kernels, path, failures):
    with open(path[:-4] + ".json", encoding="utf-8") as f:
        exp = json.load(f)
    want = [[cell.encode("utf-8").decode("latin-1") for cell in row] for row in exp["rows"]]
    want_err = exp["strict_error"]
    for kernel in kernels:
        for mode in MODES:
            rows, err = dump(build, path, kernel, mode)
            if rows != want or err:
                failures.append(f"{os.path.basename(path)} lenient {kernel} {mode}: got {rows} {err}")
            rows, err = dump(build, path, kernel, mode, strict=True)
            if want_err is None and (rows != want or err):
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: got {rows} {err}")
            elif want_err is not None and err != want_err:
                failures.append(f"{os.path.basename(path)} strict {kernel} {mode}: error {err}, want {want_err}")
    return len(kernels) * len(MODES) * 2


def test(build):
    print("differential suite", flush=True)
    subprocess.run([os.path.join(build, "differential")], check=True)
    kernels = subprocess.run([os.path.join(build, "dump"), "-l"], capture_output=True, check=True).stdout.decode().split()
    print(f"kernels: {' '.join(kernels)}")
    failures, runs = [], 0
    for name in sorted(os.listdir(CSV_DIR)):
        if not name.endswith(".csv"):
            continue
        before = len(failures)
        runs += check(build, kernels, os.path.join(CSV_DIR, name), failures)
        status = "ok" if len(failures) == before else "FAIL"
        print(f"  {status:4} {name}")
    for f in failures:
        print("FAIL:", f)
    print(f"{runs} parses, {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "test":
        print(__doc__)
        sys.exit(2)
    sys.exit(test(sys.argv[2]))
