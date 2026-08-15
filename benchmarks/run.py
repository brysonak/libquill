import argparse
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, "build", "benchmarks")
EXE = ".exe" if os.name == "nt" else ""
CC = os.environ.get("CC", "clang")


def sh(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"`{' '.join(cmd[:3])}` failed:\n{r.stderr or r.stdout}")
    return r.stdout


def out(name):
    os.makedirs(BUILD, exist_ok=True)
    return os.path.join(BUILD, name + EXE)


def build_quill():
    exe = out("quill")
    sh([CC, "-std=c11", "-O2", "-I" + os.path.join(ROOT, "include"),
        os.path.join(HERE, "quill", "bench.c"), "-o", exe])
    return [exe]


CONTENDERS = [
    ("quill", build_quill, [], "1 thread"),
]


def run(cmd, runs):
    best, text = float("inf"), None
    for _ in range(runs):
        t = time.perf_counter()
        r = subprocess.run(cmd, capture_output=True, text=True)
        best = min(best, time.perf_counter() - t)
        if r.returncode:
            return None, (r.stderr.strip().splitlines() or ["exit %d" % r.returncode])[-1]
        text = r.stdout.strip()
    return best, text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-a", dest="actions", default="count,scan,find")
    ap.add_argument("-n", dest="name", default="Debbie")
    ap.add_argument("-f", dest="file", default=os.path.join(HERE, "big.csv"))
    a = ap.parse_args()
    actions = a.actions.split(",")
    size = os.path.getsize(a.file)

    picked = []
    for label, build, extra, note in CONTENDERS:
        print(f"building {label}", file=sys.stderr)
        picked.append((label, build() + extra, note))

    print(f"{os.path.basename(a.file)}: {size / 1e6:.0f} MB, best of 3, find {a.name!r}\n")
    for action in actions:
        args = [action, a.file] + ([a.name] if action == "find" else [])
        results = []
        for label, cmd, note in picked:
            print(f"  {action} {label}", file=sys.stderr)
            secs, text = run(cmd + args, 3)
            results.append((label, note, secs, text))
        print(f"{action}:")
        fastest = min((r[2] for r in results if r[2]), default=None)
        for label, note, secs, text in sorted(results, key=lambda r: r[2] or float("inf")):
            if secs is None:
                print(f"  {label:<16} failed: {text}")
                continue
            print(f"  {label:<16} {secs * 1e3:9.0f} ms {size / secs / 1e9:6.2f} GB/s"
                  f" {secs / fastest:6.1f}x  {note}  {text}")
        print()


if __name__ == "__main__":
    main()
