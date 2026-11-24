import csv
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from run import dump

DELIMS = [",", ";", "\t", "a", None]


def python_rows(text, delim):
    if text.startswith("\xef\xbb\xbf"):
        text = text[3:]
    csv.field_size_limit(sys.maxsize)
    return [row or [""] for row in csv.reader(io.StringIO(text, newline=""), delimiter=delim, strict=True)]


def main(build, corpus):
    tmp = os.path.join(build, "crosscheck.csv")
    compared = skipped = 0
    failures = []
    for name in sorted(os.listdir(corpus)):
        with open(os.path.join(corpus, name), "rb") as f:
            data = f.read()
        delim = DELIMS[data[0] % 5] if data else None
        if delim is None:
            skipped += 1
            continue
        with open(tmp, "wb") as f:
            f.write(data[1:])
        rows, err = dump(build, tmp, delim, strict=True)
        if err:
            skipped += 1
            continue
        try:
            want = python_rows(data[1:].decode("latin-1"), delim)
        except csv.Error as e:
            failures.append(f"{name}: quill accepts, Python rejects: {e}")
            continue
        compared += 1
        if rows != want:
            failures.append(f"{name}: quill {rows!r}\n    python {want!r}")
    if os.path.exists(tmp):
        os.remove(tmp)
    for f in failures:
        print("FAIL", f)
    print(f"{compared} compared with Python's csv, {skipped} skipped as malformed, {len(failures)} differ")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
