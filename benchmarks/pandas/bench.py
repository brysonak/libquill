import sys

import pandas as pd

action, path = sys.argv[1], sys.argv[2]
name = sys.argv[3] if len(sys.argv) > 3 else ""
opts = dict(header=None, dtype=str, keep_default_na=False, encoding="latin-1")
if action == "count":
    print(f"rows {len(pd.read_csv(path, usecols=[0], **opts))}")
    sys.exit()
df = pd.read_csv(path, **opts)
if action == "scan":
    size = sum(int(df[c].str.len().sum()) for c in df.columns)
    print(f"rows {len(df)} fields {df.size} bytes {size}")
else:
    print(f"matches {int((df == name).to_numpy().sum())}")
