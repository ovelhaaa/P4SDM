# M19P physical SD qualification tools

These tools create an **ignored temporary firmware copy** at `.pio/m19p`.
Production builds do not include the serial console or fixture generator.
The console runs on the existing UI owner and sends ordinary audio commands;
WAV creation, directory inspection and controlled project-file mutations run
only on the existing storage owner. The task stack remains 7000 bytes.

Start from the repository root with exclusive access to COM13. Preparation
does not flash or write the card:

```powershell
.venv/Scripts/python.exe tests/physical_sd/prepare.py
$env:PYTHONIOENCODING='utf-8'
& 'C:/.platformio/penv/Scripts/python.exe' -m platformio run -d .pio/m19p -e guition_app -t upload --upload-port COM13
```

Adjust the local PlatformIO interpreter path if needed. Use a Python environment
with pyserial for the host runner. Every mode writes the received serial output
and host commands to a separate `docs/GUITION_M19_PHYSICAL_SD_*_SERIAL.log`.

```powershell
.venv/Scripts/python.exe tests/physical_sd/run.py seed
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_loads
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_slicing
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_stale
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_playback
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_save
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_restore
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_missing
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_fallback
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_cycles
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_integrated
.venv/Scripts/python.exe tests/physical_sd/run.py fixed_dense
.venv/Scripts/python.exe tests/physical_sd/analyze.py
```

`seed` creates nine synthetic files with reserved `m19p_` names, preserving any
existing file of the same name. The runner's numeric sample indexes require the
initially empty directory/order recorded in M19P; inspect the inventory before
running on a different card. It never formats the card. `save` writes only the
reserved `M19P_SD_QUAL` project via the normal Project V2 service. Repeated runs
advance its A/B generations; they do not delete another project.

`missing` temporarily renames only the generated snare WAV, loads the project,
checks behavior, then restores its name and reloads. `fallback` backs up the
newest slot of `M19P_SD_QUAL`, flips one payload byte, leaves the older slot
untouched, loads, then restores the backed-up file and removes the backup.
If either mode is interrupted, use `console STORE 4` to restore the snare name
or `console STORE 6` to restore the backed-up project slot. Do not use mutation
modes on a pre-existing project of that name that you wish to preserve.

Restore the production `guition_app` build after qualification. The serial
console disappears in normal firmware. The fixtures and named test project
remain on SD for physical touchscreen/listening checks.

After building the production environments, `capture.py` flashes their actual
unmodified images and records a fresh boot and serial window (including flash
verification). Example, using exclusive COM13 access:

```powershell
.venv/Scripts/python.exe tests/physical_sd/capture.py guition_transient_stress 90 docs/GUITION_M19_PHYSICAL_SD_FIXED_STRESS_SERIAL.log
.venv/Scripts/python.exe tests/physical_sd/capture.py guition_transient_synth_stress 90 docs/GUITION_M19_PHYSICAL_SD_FIXED_SYNTH_STRESS_SERIAL.log
.venv/Scripts/python.exe tests/physical_sd/capture.py guition_app 66 docs/GUITION_M19_PHYSICAL_SD_FIXED_NORMAL_SERIAL.log
```

These capture commands replace their named log file; keep earlier evidence
under another name when comparing runs. Use the unchanged `tests/analyze_m19.py`
with `--synth` or `--normal` as appropriate. The M19P report records an exact
startup-heap equality failure in fresh mounted-card captures; do not label a
failed analyzer run as PASS.

These are scripted physical-filesystem tests. They do **not** establish physical
touch usability, waveform appearance, audible quality, or MED suitability on
real musical recordings. RTS reset verifies persistence across a fresh board
boot; electrical power removal and abrupt power loss during writes are separate.
Reset uses esptool and requires a new application SD mount/index. Override
`P4SDM_ESPTOOL_PYTHON` and `P4SDM_ESPTOOL` for different installations.
`analyze.py` checks the committed physical captures; retried logs retain all
attempts but it explicitly evaluates the last host session. It does not replace
or relax the existing normal/M19 stress analyzers.

The temporary measurement window records up to 16384 render times (p99), maximum
over all window blocks, deadline misses, I2S failures/timeouts, nonzero output,
maximum simultaneous PCM voices, and analysis overlap. It supplements, and does
not replace or relax, the existing M19 stress/analyzer tests. The console,
paced serial status output, and instrumentation themselves add overhead.
