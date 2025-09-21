import gzip
from pathlib import Path

path = Path("traces/LONG-SPEC2K6-01.cbp4.gz")

with gzip.open(path, "rb") as f:
    pc = f.read(4)
    target = f.read(4)
    op_type = f.read(1)
    taken = f.read(1)
    print(pc, target, op_type, taken)
