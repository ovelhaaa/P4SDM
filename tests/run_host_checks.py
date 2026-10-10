"""Run the repository's CI host checks on Windows or Linux without changing CI.

Build checks remain in the existing PlatformIO job. Temporary host binaries and
fixtures use .pio/host-checks; historical device logs remain historical evidence.
"""
from pathlib import Path
import shlex
import subprocess
import sys
root = Path('.pio/host-checks').resolve()
root.mkdir(parents=True, exist_ok=True)
checks = []
for workflow in Path('.github/workflows').glob('*.yml'):
    for line in workflow.read_text().splitlines():
        command = line.strip().removeprefix('- run: ')
        if not command.startswith(('g++ ', 'python ')):
            continue
        # Firmware artifact packaging depends on the separate PlatformIO job.
        if command.startswith(('python tests/archive_firmware.py ',
                               'python tests/m214_ci_build.py')):
            continue
        checks.append(command)
failed = []
for number, command in enumerate(checks, 1):
    print(f'[{number}/{len(checks)}] {command}', flush=True)
    try:
        for part in command.split(' && '):
            args = shlex.split(part)
            args = [str(root / arg[5:]) if arg.startswith('/tmp/') else arg for arg in args]
            if args[0] == 'python': args[0] = sys.executable
            subprocess.run(args, check=True)
    except subprocess.CalledProcessError:
        failed.append(command)
print(f'Host CI checks: {len(checks)-len(failed)}/{len(checks)} PASS', flush=True)
for command in failed: print('FAILED:', command)
sys.exit(bool(failed))
