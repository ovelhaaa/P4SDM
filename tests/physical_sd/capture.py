from pathlib import Path
import subprocess, os, sys, time, datetime
import serial
sys.stdout.reconfigure(encoding='utf-8')

root = Path(__file__).resolve().parents[2]
env = os.environ.copy()
env['PYTHONIOENCODING'] = 'utf-8'
python = os.environ.get('P4SDM_ESPTOOL_PYTHON', r'C:\.platformio\penv\Scripts\python.exe')
esptool = os.environ.get('P4SDM_ESPTOOL', r'C:\.platformio\penv\Scripts\esptool.exe')
name, seconds, output = sys.argv[1], float(sys.argv[2]), root / sys.argv[3]
build = root / '.pio/build' / name
args = [python, esptool, '--chip', 'esp32p4', '--port', 'COM13', '--baud', '460800', '--before', 'default-reset', '--after', 'hard-reset', 'write-flash', '-z', '--flash-mode', 'dio', '--flash-freq', '80m', '--flash-size', '16MB', '0x2000', str(build/'bootloader.bin'), '0x8000', str(build/'partitions.bin'), '0xe000', 'C:/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin', '0x10000', str(build/'firmware.bin')]
with output.open('wb') as log:
    log.write(f'# Production firmware {name}; fresh esptool flash/hardware reset; UTC {datetime.datetime.now(datetime.timezone.utc).isoformat()}\n'.encode())
    result = subprocess.run(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    log.write(result.stdout); log.flush()
    print(result.stdout.decode('utf-8','replace'), flush=True)
    assert result.returncode == 0
    port = serial.Serial(port=None, baudrate=115200, timeout=.02)
    port.dtr = False; port.rts = False; port.port = 'COM13'; port.open()
    stop = time.monotonic() + seconds
    while time.monotonic() < stop:
        data = port.read(min(32768, port.in_waiting) or 1)
        if data:
            log.write(data); log.flush()
            print(data.decode('utf-8','replace'), end='', flush=True)
    port.close()
