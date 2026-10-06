"""Capture genuine native USB/JTAG serial bytes; do not synthesize log lines."""
import argparse
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM13')
parser.add_argument('--output', required=True)
parser.add_argument('--seconds', type=float, default=180)
args = parser.parse_args()
with serial.Serial(args.port, 115200, timeout=0.2) as port:
    port.dtr = False
    port.rts = True
    time.sleep(0.2)
    port.rts = False
    deadline = time.monotonic() + args.seconds
    with Path(args.output).open('wb') as log:
        while time.monotonic() < deadline:
            chunk = port.read(max(1, port.in_waiting))
            if chunk:
                log.write(chunk)
                log.flush()
print(f'Captured {Path(args.output).stat().st_size} serial bytes to {args.output}')
