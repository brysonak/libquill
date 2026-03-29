import argparse
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORDS = ("alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima "
          "mike november oscar papa quebec romeo sierra tango uniform victor whiskey "
          "xray yankee zulu").split()
UNICODE = ["café", "naïve", "Zürich", "東京", "Москва", "🦆", "ñandú", "Ελλάδα"]
DEFAULT_SET = [("dense", "1M"), ("dense", "64M"), ("sparse", "64M"), ("mixed", "256M"), ("hostile", "32M")]


def parse_size(text):
    units = {"K": 1 << 10, "M": 1 << 20, "G": 1 << 30}
    text = text.strip().upper()
    return int(float(text[:-1]) * units[text[-1]]) if text[-1] in units else int(text)


def quote(s, delim):
    if any(c in s for c in (delim, '"', "\n", "\r")) or s.startswith(" "):
        return '"' + s.replace('"', '""') + '"'
    return s


def pool(profile, delim, rng, n=8192):
    out = []
    if profile == "hostile":
        n = 64
    for _ in range(n):
        kind = rng.random()
        if profile == "hostile":
            if kind < 0.25:
                lines = (f"{rng.randrange(1000)}{delim}{rng.choice(WORDS)}{delim}x" for _ in range(rng.randrange(5000, 20000)))
                s = "\n".join(lines)
                if rng.random() < 0.2:
                    s += ' said "hi"'
            else:
                s = rng.choice(WORDS)
        elif profile == "dense":
            if kind < 0.4:
                s = str(rng.randrange(10 ** rng.randrange(1, 9)))
            elif kind < 0.6:
                s = f"{rng.uniform(-1e4, 1e4):.{rng.randrange(1, 5)}f}"
            elif kind < 0.7:
                s = f"20{rng.randrange(10, 30)}-{rng.randrange(1, 13):02}-{rng.randrange(1, 29):02}"
            elif kind < 0.9:
                s = rng.choice(WORDS)
            else:
                s = f"{rng.choice(WORDS)}{delim} {rng.choice(WORDS)}"
        elif profile == "sparse":
            words = rng.choices(WORDS + UNICODE, k=rng.randrange(20, 200))
            if rng.random() < 0.3:
                words.insert(rng.randrange(len(words)), "\n")
            if rng.random() < 0.2:
                words.insert(rng.randrange(len(words)), '"quoted"')
            s = " ".join(words)
        else:
            if kind < 0.1:
                s = ""
            elif kind < 0.3:
                s = str(rng.randrange(1 << 32))
            elif kind < 0.5:
                s = rng.choice(UNICODE + WORDS)
            elif kind < 0.6:
                s = f'he said "{rng.choice(WORDS)}"'
            elif kind < 0.7:
                s = f"{rng.choice(WORDS)}\r\n{rng.choice(WORDS)}"
            elif kind < 0.95:
                s = " ".join(rng.choices(WORDS, k=rng.randrange(2, 12)))
            else:
                s = " ".join(rng.choices(WORDS + UNICODE, k=rng.randrange(200, 2000)))
        out.append(quote(s, delim))
    return out


def generate(path, size, profile, seed, delim, crlf, bom):
    rng = random.Random(seed)
    cols = {"dense": 12, "sparse": 4, "mixed": 8, "hostile": 3}[profile]
    fields = pool(profile, delim, rng)
    eol = "\r\n" if crlf else "\n"
    written = 0
    with open(path, "wb") as f:
        if bom:
            written += f.write(b"\xef\xbb\xbf")
        while written < size:
            rows = [delim.join(rng.choices(fields, k=cols)) + eol for _ in range(4096)]
            data = "".join(rows).encode()
            if written + len(data) > size:
                for row in rows:
                    written += f.write(row.encode())
                    if written >= size:
                        break
            else:
                written += f.write(data)
    return path


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("size", nargs="?")
    ap.add_argument("--profile", choices=("dense", "sparse", "mixed", "hostile"), default="mixed")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--tsv", action="store_true")
    ap.add_argument("--crlf", action="store_true")
    ap.add_argument("--bom", action="store_true")
    ap.add_argument("--out")
    a = ap.parse_args()
    delim = "\t" if a.tsv else ","
    jobs = [(a.profile, a.size)] if a.size else DEFAULT_SET
    for profile, size in jobs:
        path = a.out or os.path.join(HERE, "csv", f"gen_{profile}_{size}.{'tsv' if a.tsv else 'csv'}")
        generate(path, parse_size(size), profile, a.seed, delim, a.crlf, a.bom)
        print(f"{path}  {os.path.getsize(path) / 1e6:.1f} MB", file=sys.stderr)


if __name__ == "__main__":
    main()
