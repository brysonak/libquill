import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV_DIR = os.path.join(HERE, "csv")
MODES = ["whole", "stream:1", "stream:7", "stream:64", "stream:4096"]


def dump(build, path, mode="whole"):
    out = subprocess.run([os.path.join(build, "dump"), "-m", mode, path], capture_output=True, check=True).stdout
    return [json.loads(line) for line in out.decode().splitlines()]


def check(build, path, failures):
    with open(path[:-4] + ".json", encoding="utf-8") as f:
        want = [[cell.encode("utf-8").decode("latin-1") for cell in row] for row in json.load(f)["rows"]]
    for mode in MODES:
        rows = dump(build, path, mode)
        if rows != want:
            failures.append(f"{os.path.basename(path)} {mode}: got {rows}")
    return len(MODES)


def test(build):
    failures, runs = [], 0
    for name in sorted(os.listdir(CSV_DIR)):
        if not name.endswith(".csv"):
            continue
        before = len(failures)
        runs += check(build, os.path.join(CSV_DIR, name), failures)
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
