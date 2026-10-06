# M8 — Groove engine

Implemented on `codex/m8-groove-engine`, continuing clean M7 commit `bd87e44`.
Implementation commit: `2b81680`. Remote inspection still finds `origin/main`
at M6.1 `ef941a7` and `origin/codex/m7-pattern-engine` at `bd87e44`; this is a
stacked M8 branch based directly on M7, and neither milestone is merged by this
task. Local commits are not pushed and remote GitHub CI is not claimed.
M8 ends here: no persistence, parameter locks, automation, microtiming, song mode,
MIDI, recording, networking or physical SD qualification.

## State, memory and ownership

`StepMeta` stores three uint8 values: velocity 1–127 (default 100), probability
0–100 (default 100), ratchets 1–4 (default 1). `Pattern` retains its sixteen uint16
trigger masks and length 1–16, adding a fixed [16 tracks][16 steps] metadata array.
It remains trivially copyable: 802 bytes including alignment; sixteen slots use
12,832 bytes. The increase is 12,288 bytes per Engine, or 24,576 bytes across audio
and optimistic UI mirrors. Pending ratchet state uses 48 bytes per Engine, with
bounded scalar clock/PRNG/counters separately. Event gains use 32 bytes. There are
no per-step objects, heap events, additional queues or allocations in audio.

`Command` remains 12 bytes. Groove edits use existing padding for explicit step
intent alongside pattern and track; the kinds are Velocity, Probability, Ratchet
and global Swing. Existing bounded SPSC queues and maximum sixteen commands per
audio block remain. Audio alone owns realtime state; rejected commands do not
apply optimistic UI edits. Atomic transport acknowledgement, sample Transfer,
source-aware pitch, track parameters, Delay, touch and NativeQueued display remain.
Serializable masks/length/meta and global swing exclude RNG, pending events, UI
state and sample pointers.

## Velocity and accent

`TriggerEvent {track, velocity}` is a two-byte value. Retrigger replaces that
track's event gain immediately. Manual audition always passes 127 and bypasses
probability/mute/solo, independent of selected step metadata.

At trigger time Q15 gain = floor(velocity × 32768 / 127). The shared app mixer hook
attenuates either wavetable/envelope output or resident PCM before existing filter,
track volume/pan and FX sends. Signed int32 multiplication/division by 32768
handles -32768 and +32767 without wrapping. Velocity 127 is exact unity; other
velocities attenuate. No per-sample floating-point work is added, resident PCM is
unchanged and persistent track volume is untouched. ACCENT is a shortcut only:
127 toggles to 100; any other velocity toggles to 127. Highlight derives solely
from velocity == 127. The retained filter/Delay can have existing sounding tails;
there is no retained gain from an earlier hit.

## Sample clock and swing

The M7 rational clock accumulates BPM × 4 units per sample; one straight sixteenth
is D = 44,100 × 60 = 2,646,000 units. M8 retains residual phase across onsets.
Long duration = D × 2 × swing / 100; short = D × 2 × (100 − swing) / 100.
D is divisible by 100, so every integer swing value is exact. Each complete pair
totals 2D and cannot drift. At 50% timing is exactly M7, including non-divisor BPM.
Swing is latched at even-step onset for the entire pair, so a live edit cannot
change that pair's total duration. BPM edits change phase speed at block boundaries.

Pairing derives from pattern step position, restarting with a long step at every
loop/switch. Odd lengths end with an unpaired long step: loop duration is
floor(length / 2) × 2D + long_duration. Length 1 repeats long steps. This deliberate
reset policy does not promise straight-duration odd loops above 50% swing;
complete pairs still retain exact master tempo. Length/switch ownership stays M7.

## Probability, ratchets and transport

Every eligible active parent consumes one xorshift32 draw, including 0/100%.
Compare uint64(draw) × 100 against probability × 2^32, without modulo bias.
Default transport seed is `0x4d385031`; seed zero maps to it to avoid the xorshift
zero trap. Failed parents create no initial or ratchet hits. One stream continues
through switches, determined by pattern, seed and performance/transport history.

Accepted parents snapshot velocity and ratchet count. Each track retains only
velocity/count/next index; sixteen tracks have at most 48 pending additional hits.
The earliest due offset is cached; sixteen tracks are scanned only on a due
subdivision. Offset k = ceil(k × actual swung duration / N) rational units, fired
at the first sample whose phase reaches it. Onset residual is retained, spreading
rounding instead of accumulating truncated intervals. This can differ by one
sample from dividing an already-rounded sample interval. Every subdivision is
strictly before the next onset at supported tempos. No generic priority queue,
wall-clock timer or UI command per hit.

Parent mute/solo/probability decisions are captured at onset. Subsequent mute,
solo, metadata, CLEAR or COPY commands leave accepted pending subhits intact and
affect the next parent. STOP cancels future subhits and retains voice/Delay tails.
PLAY clears pending state, resets phase/step, selects the edit pattern and resets
PRNG to transport seed. A switch finishes old subhits before its boundary, clears
old schedules at that boundary and starts new step zero with long swing phase,
without reseeding. Repeated PLAY reproduces probability.

## STEP UI and rendering

SEQ taps still toggle immediately and select the step. White edges identify
selected steps even when active; yellow playhead remains separate. STEP opens
a dedicated editor showing pattern/track/step and ON/OFF, numeric VEL, ACCENT,
PROB and four explicit 1x/2x/3x/4x buttons. VEL/PROB and global sequence SWING
use drag controls. Targets are at least 52 pixels high. STEP/SWING fit between
the grid and pads; pads move down 30 pixels without shrinking.

Edits dirty only their controls: velocity also dirties accent; ratchet dirties
the four choices. Metadata never requests full-frame reconstruction. Initial/page
transitions retain full draws. Existing atomic per-track flashes coalesce subhits;
probability has no animation and ratchets do not flash a whole page or flood UI.
The 63-second UI report drains its final presentation before taking frame counts.

COPY assigns the complete bounded Pattern, including length and all metadata;
destination edits are independent. CLEAR zeros masks only. Disabled/hidden steps
retain metadata, including across length shortening and later re-enabling.

## Host tests and CI

`groove_engine_test.cpp` checks exact samples at BPM 97/123/240/400, swing
50/55/60/66/75, lengths 1/3/5/7/16, all four ratchet counts and 2,048 parent
onsets for pair drift. It checks first/final steps, switch cleanup, pending hits
after mute, STOP, deterministic restart, seed differences, an independent exact
PRNG/probability/subhit reference, 0/100%, pad velocity, metadata defaults/clamps,
retention, copy independence and queued explicit edit intent. Signed full-scale,
near-zero and resident PCM scaling use the same gain helper as synth. STEP/SEQ
geometry checks bounds, large targets, all pairwise overlaps and dirty region size.
All existing M7 exact-clock and transport tests remain intact.

Eight C++17 host binaries pass with Wall/Wextra/Werror. Previous eight-FX baseline
comparison and M4/M4.1/M5/M6/M6.1/M7 recorded-log analyzers pass. CI preserves its
ten firmware environments and adds groove host tests plus `analyze_m8.py` for
genuine device evidence. The analyzer rejects deadline/transport/rails/command
rejects/heap changes, missing groove exercise, insufficient headroom or missing
M7 transitions/actions. No SD dependency is introduced. Local workflow execution
and remote GitHub CI status are reported separately.

Local CI-equivalent result: **PASS** for all ten firmware environments
(`guition_boot`, `guition_audio`, `guition_synth`, `guition_fx`,
`guition_display`, `guition_ui_audio`, `guition_ui_audio_m41`, `guition_app`,
`guition_app_stress`, `guition_sampler_stress`), all eight host binaries, original
FX digest comparison and every prior workflow log analyzer plus M8. The nine-env
regression build completed successfully in 6m34s; stress was separately built,
uploaded and qualified. Existing pinned-framework warnings remain. Remote CI is
not run because the local branch has not been pushed.

## Genuine hardware qualification

Capture: `GUITION_M8_STRESS_SERIAL.log`, 2026-10-06, genuine USB/JTAG COM13,
ESP32-P4 revision 1.3, existing pinned 360MHz configuration. The M8 analyzer
passes. The unchanged loader reports one unsuccessful no-card mount; all stress
voices use synth. First 2,068 audio blocks use all sixteen tracks, all steps,
velocity 127, probability 100%, 4x ratchets, 240 BPM, straight swing and Delay.
Display/playhead/flashes and touch polling remain active. Then the existing
12-second M7 script exercises length-16/length-7 switching, latest queue wins,
copy/inspect/clear, length edits, mute/multi-solo, pads, steps and page changes.
Mixed pattern metadata exercises velocity 1–127, probability 0/65/100%, ratchets
1–4 and swing transitions 60/75%; STEP velocity/probability/accent/ratchet edits
are included. No SD read/load operation is needed by the script.

| Measurement | Final result |
|---|---:|
| Audio capture | 10,337 × 256 frames = 60.005 s |
| UI interval | 63.024 s |
| Render p50 / p95 / p99 / max | 3,210 / 3,962 / 4,008 / 4,092 µs |
| Audio deadline / minimum headroom | 5,805 µs / 29.5% |
| Deadline misses / write errors / timeouts | 0 / 0 / 0 |
| PCM peak / nonzero values / rails | 7,598 / 5,290,884 / 0 |
| Voice min / max | 16 / 16 |
| Eligible parent steps | 14,992 |
| Probability passed / skipped | 13,785 / 1,207 |
| Ratchet additional triggers | 39,004 |
| Total sequencer voice triggers | 52,789 |
| Maximum pending additional ratchets | 48 |
| Event velocity min / max | 1 / 127 |
| Swing transitions | 8 |
| Pattern switches / wraps | 8 / 76 |
| 16→7 / 7→16 transitions | 4 / 4 |
| Queue replacements / copy / clear | 4 / 4 / 4 |
| Length / solo / mute / step commands | 16 / 8 / 8 / 8 |
| UI submitted / completed | 1,559 / 1,559 |
| Full frames (initial/page transitions) | 26 |
| Normal frames / dirty avg / dirty max | 1,533 / 61,558 / 440,320 bytes |
| All-frame dirty avg / max | 73,340 / 768,000 bytes |
| Touch errors / rejected commands | 0 / 0 |
| UI preparation max / skipped slots | 135,150 µs / 38 |
| PSRAM free before / after | 30,885,508 / 30,885,508 bytes |
| Largest PSRAM block before / after | 30,408,692 / 30,408,692 bytes |
| Internal free heap before / after | 382,020 / 382,020 bytes |
| Worst 4x period | 2,068 blocks = 12.005 s |
| Worst 4x render / headroom | 4,054 µs / 30.2% |
| Worst 4x triggers / triggers per second | 12,304 / 1,024 |

No heap growth, clipped rails, reboot, transport failure or deadline miss.
No voices, sample rate or display quality were reduced. Initial/page full frames
and alternating-buffer dirty history remain expected; normal updates never submit
a complete framebuffer. Display preparation can skip UI slots and does not affect
the sample clock; this does not claim guaranteed 30 FPS. Scripted interactions do
not establish human touch ergonomics, visual integrity or audible quality.
Physical SD/resident-sample device qualification remains pending and does not
block M8. Host PCM math is verified, but an actual SD-fed sample capture is deferred.

The final normal firmware `guition_app` was restored on COM13 and captured in
`GUITION_M8_NORMAL_BOOT_SERIAL.log`. Its 60.005-second idle audio capture has
p50/p95/p99/max 1,858/1,870/1,874/1,982 µs, zero misses/write failures/timeouts,
zero PCM activity/rails and no unintended triggers. The 63.002-second UI interval
submitted/completed one initial frame, with no further redraws, touch errors or
rejects. PSRAM remains 30,885,508 bytes, largest block 30,408,692 bytes, and internal
heap 382,036 bytes before/after. The normal capture passes `analyze_m5.py` and the
device is left running this normal firmware, without automatic stress transport.

## Readiness

Yes: the implemented musical controls, exact deterministic scheduling, bounded
ownership and qualified audio margin make M8 a suitable basis for later parameter
locks and project persistence. This conclusion applies to the measured synth,
Delay and display/touch realtime envelope; sample-device and human audible/UI
qualification remain the stated limitations. None of those later features starts
automatically.

## Changed files

- `src/app/model.h`: metadata, compact intent, gain math, clock/scheduler and geometry.
- `src/app_main.cpp`: velocity path, STEP/swing controls, dirty rendering, stress/telemetry.
- `synthESP32.ino`: three-line app-only shared synth/PCM gain hook.
- `tests/groove_engine_test.cpp`, `tests/analyze_m8.py`: exact host and device checks.
- `.github/workflows/guition.yml`: M8 tests beside all previous checks.
- `README.md`, this report, genuine stress/normal boot logs: workflow and evidence.
