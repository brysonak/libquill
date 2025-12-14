import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CSV_DIR = os.path.join(HERE, "csv")


def dump(build, path):
    out = subprocess.run([os.path.join(build, "dump"), path], capture_output=True, check=True).stdout
    return [json.loads(line) for line in out.decode().splitlines()]


def test(build):
    failures = 0
    for name in sorted(os.listdir(CSV_DIR)):
        if not name.endswith(".csv"):
            continue
        path = os.path.join(CSV_DIR, name)
        with open(path[:-4] + ".json", encoding="utf-8") as f:
            want = [[cell.encode("utf-8").decode("latin-1") for cell in row] for row in json.load(f)["rows"]]
        rows = dump(build, path)
        ok = rows == want
        failures += not ok
        print(f"  {'ok' if ok else 'FAIL':4} {name}")
        if not ok:
            print(f"       got  {rows}\n       want {want}")
    return 1 if failures else 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "test":
        print(__doc__)
        sys.exit(2)
    sys.exit(test(sys.argv[2]))
