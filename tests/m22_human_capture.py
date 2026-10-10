"""Read serial while a human operates the LCD; U requests software metrics only."""
import argparse
from pathlib import Path
import subprocess
import time
import serial
p=argparse.ArgumentParser();p.add_argument('--seconds',type=float,default=360);p.add_argument('--port',default='COM13');p.add_argument('--source-sha',required=True,help='Exact firmware archive source SHA');p.add_argument('--output',type=Path,default=Path('docs/m22/HUMAN_CONTROLLER_SERIAL.log'));a=p.parse_args()
port=serial.Serial(port=None,baudrate=115200,timeout=.05);port.dtr=False;port.rts=False;port.port=a.port
port.open()
with port,a.output.open('wb') as log:
    sha=a.source_sha
    log.write(f'# Human walkthrough capture armed; source {sha}; no scripted touch injection. Human observations must be supplied separately. U only reads timing histograms.\n'.encode())
    stop=time.monotonic()+a.seconds;query=0
    while time.monotonic()<stop:
        if time.monotonic()>=query:
            port.write(b'U');query=time.monotonic()+15
        data=port.read(min(32768,port.in_waiting) or 1)
        if data: log.write(data);log.flush()
print('Controller packets/software timing captured; physical UX pass requires the human report.')
