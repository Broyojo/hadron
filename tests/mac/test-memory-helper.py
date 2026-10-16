#!/usr/bin/env python3
"""Validate the built native footprint reader, including its exited-process fallback."""
import os
import sys
from pathlib import Path
import subprocess

helper = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / "dist/bin/hadron-memory"
assert subprocess.check_output([helper], text=True).strip() == "0"
assert subprocess.check_output([helper, "2147483647:2048"], text=True).strip() == "2"
assert int(subprocess.check_output([helper, f"{os.getpid()}:0"], text=True)) > 0
for value in ("0:10", "-1:10", "1:", "1:-1", "1:2x", "2147483648:1",
              "1:18446744073709551615", "1:1:1"):
    assert subprocess.run([helper, value], capture_output=True).returncode != 0, value
print("memory helper: footprint, fallback, and invalid-input checks passed")
