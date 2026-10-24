#!/usr/bin/env python3
"""Write map JSON paths to a newline-delimited response file."""
import sys
from pathlib import Path

if len(sys.argv) != 3:
    raise SystemExit("Usage: write_map_response.py OUTPUT MAPS_DIR")
output = Path(sys.argv[1])
root = Path(sys.argv[2])
paths = sorted(root.glob("*/map.json"))
if not paths:
    raise SystemExit(f"No map.json files found under {root}")
output.write_text("".join(p.as_posix() + "\n" for p in paths), encoding="utf-8")
print(f"Wrote {len(paths)} map paths to {output}")
