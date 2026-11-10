import sys

import duckdb

action, path = sys.argv[1], sys.argv[2]
name = sys.argv[3] if len(sys.argv) > 3 else ""
src = "read_csv($path, header = false, all_varchar = true, delim = ',', quote = '\"', escape = '\"', strict_mode = false)"
db = duckdb.connect()
if action == "count":
    print(f"rows {db.execute(f'SELECT count(*) FROM {src}', dict(path=path)).fetchone()[0]}")
    sys.exit()
cols = [r[0] for r in db.execute(f"DESCRIBE SELECT * FROM {src}", dict(path=path)).fetchall()]
if action == "scan":
    expr = " + ".join(f'sum(strlen(coalesce("{c}", \'\')))' for c in cols)
    rows, size = db.execute(f"SELECT count(*), {expr} FROM {src}", dict(path=path)).fetchone()
    print(f"rows {rows} fields {rows * len(cols)} bytes {size}")
else:
    expr = " + ".join(f'count_if("{c}" = $name)' for c in cols)
    print(f"matches {db.execute(f'SELECT {expr} FROM {src}', dict(path=path, name=name)).fetchone()[0]}")
    