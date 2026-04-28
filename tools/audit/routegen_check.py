#!/usr/bin/env python3
from __future__ import annotations
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
routes_def = ROOT / "config" / "routes.def"
gen_h = ROOT / "core" / "generated" / "include" / "ev" / "route_table_generated.h"

route_count = 0
for raw in routes_def.read_text(encoding="utf-8").splitlines():
    line = raw.strip()
    if line.startswith("EV_ROUTE(") or line.startswith("EV_ROUTE_EX("):
        route_count += 1

text = gen_h.read_text(encoding="utf-8")
match = re.search(r"EV_ROUTE_TABLE_GENERATED_COUNT\s+([0-9]+)U", text)
if match is None:
    print("generated route count marker missing", file=sys.stderr)
    raise SystemExit(1)
generated_count = int(match.group(1))
if generated_count != route_count:
    print(f"routegen stale: routes.def={route_count}, generated={generated_count}", file=sys.stderr)
    raise SystemExit(1)
print(f"routegen-check passed: {generated_count} routes")
