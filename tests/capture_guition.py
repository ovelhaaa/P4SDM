"""Capture genuine native USB/JTAG serial bytes; do not synthesize log lines."""
import argparse
import time
from pathlib import Path
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', default='COM13')
parser.add_argument('--output', required=True)
parser.add_argument('--seconds', type=float, default=180)
parser.add_argument('--no-reset', action='store_true')
parser.add_argument('--append', action='store_true')
parser.add_argument('--command', choices=['L','S'], help='M4.1 startup run selection')
args = parser.parse_args()
connection = serial.Serial()
connection.port = args.port
connection.baudrate = 115200
connection.timeout = 0.2
connection.dtr = not args.no_reset
connection.rts = not args.no_reset
connection.open()
with connection as port:
    if not args.no_reset:
        # Windows propagates the native USB/JTAG DTR state on RTS changes.
        port.dtr = False
        port.rts = True
        time.sleep(0.2)
        port.rts = False
    if args.command:
        time.sleep(4)
        port.write(args.command.encode('ascii'))
    deadline = time.monotonic() + args.seconds
    with Path(args.output).open('ab' if args.append else 'wb') as log:
        while time.monotonic() < deadline:
            chunk = port.read(max(1, port.in_waiting))
            if chunk:
                log.write(chunk)
                log.flush()
print(f'Captured {Path(args.output).stat().st_size} serial bytes to {args.output}')
