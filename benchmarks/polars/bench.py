import sys

import polars as pl

action, path = sys.argv[1], sys.argv[2]
name = sys.argv[3] if len(sys.argv) > 3 else ""
opts = dict(has_header=False, infer_schema=False)
if action == "count":
    print(f"rows {pl.scan_csv(path, **opts).select(pl.len()).collect().item()}")
    sys.exit()
df = pl.read_csv(path, **opts)
if action == "scan":
    size = df.select(pl.sum_horizontal(pl.all().str.len_bytes().sum())).item()
    print(f"rows {df.height} fields {df.height * df.width} bytes {size}")
else:
    hits = df.select(pl.sum_horizontal((pl.all() == name).sum())).item()
    print(f"matches {hits}")
