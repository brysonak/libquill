import csv
import sys

action, path = sys.argv[1], sys.argv[2]
name = sys.argv[3] if len(sys.argv) > 3 else ""
csv.field_size_limit(sys.maxsize)
rows = fields = size = matches = 0
with open(path, newline="", encoding="latin-1") as f:
    if action == "count":
        rows = sum(1 for _ in csv.reader(f))
    else:
        for row in csv.reader(f):
            rows += 1
            fields += len(row)
            size += sum(map(len, row))
            matches += row.count(name)
if action == "count":
    print(f"rows {rows}")
elif action == "scan":
    print(f"rows {rows} fields {fields} bytes {size}")
else:
    print(f"matches {matches}")
