import argparse
import glob
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEPS = os.path.join(HERE, ".deps")
BUILD = os.path.join(ROOT, "build", "benchmarks")
EXE = ".exe" if os.name == "nt" else ""
CC = os.environ.get("CC", "clang")
THREADS = os.cpu_count() or 1

ZSV = ("https://github.com/liquidaty/zsv.git", "2e2181930f68b6b83da69eb19d24636393d15408")
CISV = ("https://github.com/sanix-darker/cisv.git", "0d55b40918ab9c57f0b0c225161854ccdfe2fd9f")


class Skip(Exception):
    pass


def sh(cmd, cwd=None, env=None):
    r = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
    if r.returncode:
        tail = (r.stderr or r.stdout).strip().splitlines()[-3:]
        raise Skip(f"`{' '.join(cmd[:3])}` failed: {' | '.join(tail)}")
    return r.stdout


def need(tool):
    if not shutil.which(tool):
        raise Skip(f"{tool} not found")


def fetch(name, url, sha):
    dst = os.path.join(DEPS, name)
    if os.path.exists(os.path.join(dst, ".done")):
        return dst
    need("git")
    shutil.rmtree(dst, ignore_errors=True)
    os.makedirs(dst)
    sh(["git", "init", "-q"], dst)
    sh(["git", "fetch", "-q", "--depth", "1", url, sha], dst)
    sh(["git", "checkout", "-q", "FETCH_HEAD"], dst)
    return dst


def done(path):
    open(os.path.join(path, ".done"), "w").close()


def out(name):
    os.makedirs(BUILD, exist_ok=True)
    return os.path.join(BUILD, name + EXE)


def build_quill():
    need(CC)
    exe = out("quill")
    sh([CC, "-std=c11", "-O2", "-I" + os.path.join(ROOT, "include"), "-pthread",
        os.path.join(HERE, "quill", "bench.c"), "-o", exe])
    return [exe]


def build_zsv():
    need(CC)
    need("make")
    src = fetch("zsv", *ZSV)
    if not os.path.exists(os.path.join(src, ".done")):
        env = dict(os.environ, CC=CC)
        sh(["sh", "./configure"], src, env)
        sh(["make", "-C", "src", "build"], src, env)
        done(src)
    lib = glob.glob(os.path.join(src, "build", "*", "rel", "*", "lib", "libzsv.a"))
    if not lib:
        raise Skip("libzsv.a missing after build")
    exe = out("zsv")
    sh([CC, "-O2", "-I" + os.path.join(src, "include"), os.path.join(HERE, "zsv", "bench.c"), lib[0], "-o", exe])
    return [exe]


def build_cisv():
    need(CC)
    src = fetch("cisv", *CISV)
    done(src)
    exe = out("cisv")
    sh([CC, "-O3", "-march=native", "-D_GNU_SOURCE", "-I" + os.path.join(src, "core", "include"),
        os.path.join(HERE, "cisv", "bench.c"), os.path.join(src, "core", "src", "parser.c"),
        "-lpthread", "-o", exe])
    return [exe]


def build_cargo(name):
    def build():
        need("cargo")
        d = os.path.join(HERE, name)
        sh(["cargo", "build", "--release", "-q"], d)
        return [os.path.join(d, "target", "release", "bench" + EXE)]
    return build


def build_python(name, module=None):
    def build():
        if module:
            r = subprocess.run([sys.executable, "-c", f"import {module}"], capture_output=True)
            if r.returncode:
                raise Skip(f"pip install {module}")
        return [sys.executable, os.path.join(HERE, name, "bench.py")]
    return build


CONTENDERS = [
    ("quill", build_quill, [], "1 thread"),
    (f"quill -t {THREADS}", build_quill, ["-t", str(THREADS)], f"{THREADS} threads"),
    ("zsv", build_zsv, [], "default engine"),
    ("zsv -f", build_zsv, ["-f"], "SIMD engine, scan_engine=3"),
    ("cisv", build_cisv, [], "-march=native"),
    ("simd-csv", build_cargo("simd-csv"), [], "Rust"),
    ("rust-csv", build_cargo("rust-csv"), [], "Rust csv crate"),
    ("pandas", build_python("pandas", "pandas"), [], "C engine"),
    ("python-csv", build_python("python-csv"), [], "stdlib csv"),
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
    ap.add_argument("-r", dest="runs", type=int, default=3)
    ap.add_argument("-a", dest="actions", default="count,scan,find")
    ap.add_argument("-n", dest="name", default="Debbie")
    ap.add_argument("-f", dest="file", default=os.path.join(HERE, "big.csv"))
    a = ap.parse_args()
    actions = a.actions.split(",")
    size = os.path.getsize(a.file)

    picked, built, skipped = [], {}, []
    for label, build, extra, note in CONTENDERS:
        base = label.split()[0]
        if base not in built:
            print(f"building {base}", file=sys.stderr)
            try:
                built[base] = build()
            except Skip as e:
                built[base] = None
                skipped.append((base, str(e)))
        if built[base]:
            picked.append((label, built[base] + extra, note))

    print(f"{os.path.basename(a.file)}: {size / 1e6:.0f} MB, best of {a.runs}, find {a.name!r}\n")
    for action in actions:
        args = [action, a.file] + ([a.name] if action == "find" else [])
        results = []
        for label, cmd, note in picked:
            print(f"  {action} {label}", file=sys.stderr)
            secs, text = run(cmd + args, a.runs)
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
    for base, why in skipped:
        print(f"skipped {base}: {why}")


if __name__ == "__main__":
    main()
