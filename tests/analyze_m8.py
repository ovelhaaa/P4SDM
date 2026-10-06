"""Validate genuine SD-independent M8 device qualification evidence."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("log")
parser.add_argument("--pattern-bytes", type=int, default=802)
args = parser.parse_args()
s = Path(args.log).read_text(errors="replace")

def row(prefix):
    line = next((line for line in s.splitlines() if line.startswith(prefix)), None)
    assert line, f"missing {prefix}"
    return {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", line)}

subprocess.run([sys.executable, "tests/analyze_m5.py", args.log], check=True)
audio = row("[M5]")
ui = row("[M5 UI]")
assert ui["seconds"] >= 63 and ui["submitted"] == ui["completed"]
groove = row("[M8 groove]")
worst = row("[M8 worst]")
patterns = row("[M7 patterns]")
applied = row("[M7 applied]")
assert audio["blocks"] * 256 / 44100 >= 60
assert audio["max"] <= 256 / 44100 * 1e6 * .8, "less than 20% audio headroom"
assert audio["active_max"] == 16 and audio["nonzero"] > 0
assert groove["parents"] == groove["passed"] + groove["skipped"]
assert groove["passed"] > 0 and groove["skipped"] > 0 and groove["ratchets"] > 0
assert groove["pending_max"] == 48
assert groove["velocity_min"] == 1 and groove["velocity_max"] == 127
assert groove["swing_changes"] >= 4
assert worst["blocks"] * 256 / 44100 >= 12
assert worst["bpm"] == 240 and worst["voices"] == 16 and worst["ratchet"] == 4
assert worst["swing"] == 50 and worst["delay"] == 1
assert worst["triggers_per_second"] >= 500
assert worst["max"] <= 256 / 44100 * 1e6 * .8
assert patterns["switches"] >= 4 and patterns["dirty_max"] < 768000
assert patterns["pattern_bytes"] == args.pattern_bytes and patterns["command_bytes"] <= 12
for key in ("long_to_short", "short_to_long", "queue_replacements", "copies", "clears", "lengths", "solos", "mutes", "steps"):
    assert applied[key] > 0, f"missing {key}"
assert "Guru Meditation" not in s and "INIT FAIL" not in s
print("M8 >=60s realtime groove + 12s 16-track 4x ratchet qualification: PASS")
