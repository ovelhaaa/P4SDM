"""Compare M20 with the accepted pre-M20 model/codec/audio sources."""
from pathlib import Path
import subprocess

baseline = "66fff46"
for name in ("project.h", "sample_playback.h", "slices.h", "transients.h", "voice_state.h"):
    accepted = subprocess.check_output(["git", "show", f"{baseline}:src/app/{name}"]).decode()
    assert accepted == Path("src/app", name).read_text(), name
old = subprocess.check_output(["git", "show", f"{baseline}:src/app/model.h"]).decode()
now = Path("src/app/model.h").read_text()
assert old[:old.index("struct Rect")] == now[:now.index("struct Rect")], "Engine/commands changed"
# Reuse the event, RNG, Repeat, all-eight-locks and exact V2 byte comparison,
# substituting the actual final M19/physical-SD accepted baseline.
test = Path("tests/no_transient_equivalence.py").read_text()
test = test.replace("baseline = 'bb552f8'", f"baseline = '{baseline}'")
exec(compile(test, "tests/no_transient_equivalence.py", "exec"))
print("M20 exact accepted Engine source, DSP, detector and 52,704 V2 bytes PASS")
