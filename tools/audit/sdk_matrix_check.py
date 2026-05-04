#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
completed = subprocess.run([sys.executable, str(ROOT / "tools" / "sdk_matrix.py"), "check"], cwd=ROOT)
sys.exit(completed.returncode)
