"""Validate genuine SD-independent M9 device evidence, including M8 load."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("log")
args = parser.parse_args()
data = Path(args.log).read_text(errors="replace")
subprocess.run([sys.executable, "tests/analyze_m8.py", args.log], check=True)

def row(prefix):
    lines = [line for line in data.splitlines() if line.startswith(prefix)]
    assert len(lines) == 1, f"missing or repeated {prefix}"
    return {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", lines[0])}

tools = row("[M9 tools]")
for key in ("rotate", "reverse", "copy_track", "clear_track", "duplicate",
            "euclidean", "randomize", "mutate", "reroll"):
    assert tools[key] > 0, f"missing transform {key}"
assert tools["blocks"] > 100 and tools["dense_blocks"] > 20
assert 0 < tools["max"] <= 256 / 44100 * 1e6 * .8, "transform headroom <20%"
assert tools["max"] <= row("[M5]")["max"]
ui = row("[M9 UI]")
assert ui["entries"] >= 3 and ui["copies"] >= 3
assert ui["randomize"] >= 3 and 0 < ui["cell_max"] <= 16
assert ui["edit_full"] == 0, "transform forced full-screen redraw"
print("M9 transforms + dense realtime stress: PASS")
