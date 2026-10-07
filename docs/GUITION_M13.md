# M13 — Sample Playback v2

Baseline: merged M12 `eb8456c` on `main`. Work stops after M13.

## Audit of the accepted resident player

The original `sampler::Voice` in `src/app/wav.h` has a borrowed `const Sample *`,
uint64 Q16 position and positive increment, and an active flag (32 bytes).
`Sample.frames` counts decoded mono frames. The M6 WAV loader accepts mono/stereo
16-bit PCM at 44100 Hz; stereo is averaged into one mono value per complete frame
at load time. The renderer never traverses individual stereo channel words.
There is no interpolation: it reads `data[position >> 16]`. It checks the index
against frames before reading and deactivates after increment reaches EOF.
Trigger resets position to zero, replaces increment and active status; stop
clears active and position. Retrigger has no layering. There are no fades, region,
reverse, gate or choke states. Pitch increments preserve C4=60 unity, 48=half,
72=double. The maximum accepted PCM payload is 4 MiB, so Q16 arithmetic cannot
wrap with the pitch helper's bounded increments. The old renderer does not read
past valid PCM for valid resident metadata.

`Transfer` has one staged publication slot. The storage task allocates/decodes
resident PCM, publishes it, waits for the audio acknowledgement, then frees the
retired buffer. Audio consumes at block boundary, assigns the new pointer and
stops the old voice before acknowledging. Source changes also stop the sample
voice. This ownership mechanism remains intact.

## State, endpoints and traversal

`sampler::Playback` is eight fixed bytes: normalized uint16 start/end, reverse,
OneShot/Gate mode, choke group and a reserved byte. It belongs to Track and to
accepted TriggerEvent snapshots. Defaults are 0/65535, forward, OneShot, choke 0.
First successful sample assignment resets these defaults and C4 pitch;
replacement retains intentional playback and tuning settings. Switching between
SYNTH and SAMPLE retains configured sample settings.

At each audio-owned trigger, `resolve_region` maps each normalized endpoint using
`uint64(normalized) * frames / 65535`. Start is the first playable frame; end is
exclusive. Start=65535 clamps to the last frame; end<=start is repaired to
start+1. Zero-length samples refuse playback. Nearest-neighbor reads safely
support a one-frame minimum, so two frames are not required. This minimum is a
renderer boundary contract; future interpolation must clamp its neighbors.
The UI keeps start below end independently of audio's defensive validation.

One canonical unsigned Q16 phase stores distance traveled within the region.
Forward reads start+floor(distance); reverse reads end-1-floor(distance).
Only frame direction changes: pitch magnitude remains unchanged and sub-unity
pitch repeats frames in both directions. PCM is never reversed or copied.
The renderer uses resolved bounds, direction, increment and envelope state;
normalization and division for remaining duration happen only at trigger/pitch
edit boundaries. Existing live unlocked pitch edits recompute remaining output
frames; region/direction/mode/choke edits affect future triggers only.

## Gate, choke, retrigger and accepted parents

OneShot ignores release and ends at the selected boundary. Gate accepts an
explicit addressed release and fades out; it also ends naturally at the boundary
(no looping). The sequencer releases its Gate voices at every next global
sixteenth onset for each Track, including empty/probability-rejected/muted steps.
Swing therefore determines that step's gate duration. Ratchets restart within
that same step, and transport stop releases sequenced Gate voices. Manual voices
are independent of this step release. Physical pad press uses the fast audio
queue; release retains the pressed Track identity even if page/bank/selection
changes. A full release queue retries release; a pending old release is ordered
before a new pad press. Pad release cannot release a newer sequencer-owned hit.
SYNTH and OneShot behavior retain the accepted audition semantics.

An accepted SAMPLE parent with group 1..8 scans sixteen sample voices and releases
other active voices whose accepted group matches. Group 0 scans none. Routing is
captured in the voice: later Track edits do not reroute an active hit. Rejected or
mute/solo-suppressed parents never enter this boundary. SYNTH voices are not
choked. Each ratchet retriggers its Track without rescanning other Tracks.

A same-Track trigger immediately replaces phase, resolved region, pitch,
direction, mode/group, tone/lock event, velocity and start envelope. It cancels an
old release, never layers another sample voice and does not crossfade an old
retriggered source. Ratchet origin is a separate callback argument, leaving the
accepted TriggerEvent byte-identical across parent and children. Playback state
is captured in that parent; subsequent UI/Track edits cannot change its ratchets.
Normalized settings remap if resident PCM is safely replaced between events;
accepted events never retain PCM pointers. Pattern and StepLocks storage remain
unchanged, with the same seven M12 locks.

## Boundary fades and FX

Production fades use 32 output frames and integer amplitude scaling. Attack
starts at zero and reaches unity after 32 frames. The exact remaining output
count is ceil((region_Q16-distance)/increment), resolved at trigger/pitch edit.
Natural end gain is limited by remaining-1, so the final output is zero.
Gate/choke release gain is limited by release_left-1 over 32 rendered frames.
The combined envelope uses the minimum of attack, natural-end and release gains,
preventing invalid arithmetic for overlapping short fades. Very short regions,
including one-frame regions, can be silent; they still terminate safely.
A fade-disabled argument exists only for isolated host ordering/legacy velocity
comparisons; firmware always uses the production default.

The source still feeds M12 velocity, filter, pan and effective Delay send.
Choke/release does not reset filter history or global FX. After source release,
new dry PCM becomes zero; filter history and already-generated Delay tails
continue. Replacement/source-switch retains the prior lifecycle-only filter
reset and stops release before storage can retire PCM. There is no realtime
filesystem operation, lock, allocation or per-hit Serial output.

## Fixed storage

| Structure | M12 bytes | M13 bytes |
| --- | ---: | ---: |
| Track | 40 | 48 |
| SampleVoice | 32 | 64 |
| Sixteen SampleVoices | 512 | 1024 |
| TriggerEvent | 10 | 20 |
| VoiceState | 14 | 24 |
| Engine | 46592 | 46880 |
| Pattern / sixteen-pattern bank | 2850 / 45600 | 2850 / 45600 |
| StepLocks / Command | 8 / 12 | 8 / 12 |

Track growth is 128 bytes per sixteen Tracks; each Engine grows 288 bytes,
including pending snapshots. Two Engines add 576 bytes, sixteen sample voices
add 512 and active tone/event states add 160. Counters and UI dirty bits add
small fixed storage. No per-trigger allocation occurs.

## UI and host qualification

SAMPLE opens a dedicated SAMPLE PLAYBACK page; SYNTH shows SAMPLE ONLY at the
entry. START/END use percentage values with one decimal and independent 752x56
drag targets. Direction, mode and OFF/1..8 choke have 240x56 discrete targets.
RESET REGION restores full forward playback, retaining mode, choke, assignment,
pitch and tone/send. BACK returns to SAMPLE. Region drags dirty one control;
reset dirties three. Controls stay clear of global navigation. Geometry tests
check target corners and accepted hit routing. Scripted device UI coverage does
not establish human touch/visual or audible QA.

Thirteen C++ host suites pass. The new sampler suite covers normalized lengths
1/2/3/17/1000/2000000/UINT32_MAX and endpoint combinations, known PCM ordering,
selected first/last frames, reverse, pitch 48/60/72 and extremes, tiny/inverted
regions, exact integer attack/end/release ramps, OneShot release exclusion,
Gate/choke, retrigger during release, parent snapshots after edits, next empty
step release, suppression, first assignment/replacement, stereo frame downmix,
100000 allocation-free command/voice operations, and actual filter/send/Delay
tail continuity. The original WAV producer/consumer replacement test still
poisons/frees retired buffers through 10000 publications. Its isolated order
and M8 velocity tests disable fades explicitly; production defaults do not.

All retained actual-M9/M10/M11 scheduler, routing and eight original FX
comparisons pass. Historical M4–M12 capture analyzers retain their defaults.
Explicit inherited structure-size options qualify M13. A SAMPLE analyzer flag
requires WAVE locks to remain suppressed (zero), while the retained SYNTH stress
still requires WAVE lock coverage. No historical assertion is silently relaxed.

## Device qualification

Final genuine captures, build results, firmware hashes and commits are recorded below. Physical device COM13 reports NO CARD / MOUNT ERROR.
The qualification-only build allocates a fixed 2097152-byte PSRAM pool containing sixteen distinct 65536-frame
PCM fixtures, with sixteen resident descriptors and varying region/pitch/direction.
Distinct buffers prevent shared PCM cache reuse from reducing this benchmark.
Normal firmware contains neither fixture metadata nor PCM allocation. The
original synth stress remains a separate mandatory capture.

The first twelve sample-stress seconds run all sixteen sample voices, four
ratchets, 240 BPM, seven stored M12 locks (WAVE intentionally ignored for SAMPLE),
filters, Delay, display/touch and transforms. Choke is off in that section to
avoid reducing the measured workload. Later sections exercise choke groups,
Gate release, natural ends/tiny regions, edits, mixed probability/mute/solo and
retrigger, under the same dense scheduler. Telemetry includes the minimum dense
active sample count, choke/retrigger block maxima and every new playback path.

**PHYSICAL M13 SAMPLE VALIDATION PENDING.** No mounted SD card is available.
Real WAV/DAC listening and human touch/visual QA remain pending. Host/in-memory
qualification has no SD dependency.

**Yes:** the sample voice is a bounds-safe and expressive foundation for M14
slicing and later per-step sample-region locks. A future slice/lock can supply
normalized playback state to the accepted event and existing resolved renderer
without changing PCM ownership or adding Pattern pointers. Interpolation's
neighbor rules would require separate qualification. No slicing, transients,
waveforms, new sample locks, selection locks, time-stretch, granular playback,
pitch envelopes, persistence, song mode or MIDI are implemented.

## Preserved startup failure

`GUITION_M13_SYNTH_STARTUP_FAILED_SERIAL.log` is deliberately retained as a
failed capture, not acceptance evidence. After a rapid upload/capture USB reset,
one host attempt lost the COM port and a following capture contained a startup
Store access fault, followed by automatic reboot and a complete stress report.
The strict analyzer correctly rejected that entire capture because of the panic.
MTVAL=0x500d2000 matches USB_SERIAL_JTAG in the pinned SDK peripheral linker map;
MEPC/RA were ROM addresses. It occurred during an AUDIO startup serial message,
before the M13 audio owner/voice rendering began. A later fresh controlled boot
passed the strict synth analyzer with no panic. The exact reset/console root
cause remains unproven; no sampler, HAL or task redesign masks the failure.
For the subsequent sample/normal captures, upload is allowed to settle eight
seconds before the capture tool issues its separate reset. This is a host
qualification pacing step, with no render/voice/display workload change.

## Changed files

- `src/app/sample_playback.h`: canonical playback settings and normalized region mapping.
- `src/app/wav.h`: bounds-safe bidirectional voice, fades/release, choke boundary and counters; unchanged Transfer ownership.
- `src/app/model.h`: Track/event state, addressed controls, immutable ratchet callback context, step Gate callback, pad ownership helper and UI geometry.
- `src/app/voice_state.h`: updated fixed TriggerEvent size assertion; retained tone architecture.
- `src/app_main.cpp`: audio integration, fast pad release, SAMPLE PLAYBACK UI, dirty controls, distinct PSRAM fixtures and measured telemetry.
- `platformio.ini`: qualification-only `guition_sample_playback_stress` environment.
- `tests/sample_playback_test.cpp`: DSP, lifetime, suppression, snapshots, no-allocation, pad queue and routing tests.
- `tests/analyze_m13.py`: strict sample/synth/normal acceptance, unchanged headroom deadline and heap checks.
- `tests/analyze_m10.py`, `tests/analyze_m11.py`, `tests/analyze_m12.py`: explicit inherited SAMPLE/size options with historical defaults preserved.
- `tests/groove_engine_test.cpp`, `tests/wav_sampler_test.cpp`: explicit fade-disabled isolated legacy ordering/velocity checks.
- `tests/tone_locks_test.cpp`, `tests/track_tone_test.cpp`: new event size assertions.
- `.github/workflows/guition.yml`: all retained checks, eleventh firmware environment and new host/capture checks.
- `README.md`, this report, final sample/synth/normal serial logs and preserved failed startup log: behavior and genuine qualification evidence.

## Final acceptance evidence and reproduction

Implementation commit: `b9040376b41ce17c2160d4fe799a47374e7a24a7` —
Implement M13 resident sample playback v2. A subsequent qualification commit
records this report and the four genuine final/preserved serial logs.
Branch: `codex/m13-sample-playback`; baseline main is unchanged.

All three accepted runs contain 10337 blocks (60.006 seconds of audio) and
zero deadline misses, I2S failures/timeouts, rails, UI command rejections and
heap loss. The fixed deadline is 5804.989 us; the unchanged 20% threshold is
4643.991 us. Sample and synth remain at 44100 Hz, 256 frames per block,
sixteen Tracks and the accepted display/touch pipeline.

| Run | p50/p95/p99/max us | Worst headroom | Internal heap before/after | PSRAM free before/after | Largest PSRAM before/after |
| --- | ---: | ---: | ---: | ---: | ---: |
| SAMPLE (distinct PSRAM buffers) | 2972/3395/3687/4256 | 26.68% | 312644 / 312644 | 28722816 / 28722816 | 28311540 / 28311540 |
| Retained SYNTH stress | 3387/4102/4198/4389 | 24.39% | 314500 / 314500 | 30885508 / 30885508 | 30408692 / 30408692 |
| Normal no-card idle | 1791/1813/1822/1945 | 66.49% | 314548 / 314548 | 30885508 / 30885508 | 30408692 / 30408692 |

The sample dense section has 2068 blocks (12.005 seconds), 16/16 active
SAMPLE voices throughout, 240 BPM, 4x ratchets, 1024 hits/second and max=3563 us.
Choke is disabled in this initial section to avoid reducing active voices.
Later dense-pattern sections retain ungrouped voices while exercising groups.
Across the sample run: 53518 triggers, 17082 reverse, 39911 Gate, 14122 release
requests, 3073 natural ends, 9382 accepted choke scans, 4717 voices choked and
45447 active/releasing retriggers. Resolved regions span 1..65536 source frames.
Worst choke-containing block=4218 us; worst retrigger-containing block=4256 us.
These are complete render/control block costs, not isolated instruction timings.

The sample UI has 14 entries, 65 edits, one maximum widget dirtied per measured
edit and zero edit-triggered whole-screen redraws. Submitted/completed display
transactions match. Normal boot has no fixture allocation, voices, triggers,
reverse/Gate/choke/retrigger events, PCM output, tone-lock residue or sample UI
edits. Normal `guition_app` is restored to COM13 with flash hash verification;
its 60-second no-card run passes the strict analyzer. Compared with accepted
M12 normal internal heap 315988, M13's 314548 uses 1440 additional fixed bytes;
normal PSRAM free/largest remain exactly 30885508/30408692.

Thirteen host suites, all retained scheduler/DSP equivalence and FX checks,
all historical capture analyzers and the three M13 analyzers pass locally.
All eleven pinned PlatformIO firmware environments build successfully.
The workflow retains every preceding check and adds the M13 host test,
fixture build and three accepted-capture analyzers. Hosted GitHub CI has not
been run; this branch has not been pushed or merged. The preserved failed
startup capture remains rejected and is excluded from accepted evidence.

Firmware SHA256:

- `guition_sample_playback_stress`: `d748755e95ca6be7c92843b824a537f7e82c9d518f6c799a3a4c410b3af997d6`
- `guition_app_stress`: `0c9d2f948f52978805600026416cc2126f6efa0704419f29403e2a6991076ad7`
- `guition_app`: `3edaf849938621a4a951b9c57cf543e999350082a44ba9517175ca6d52fea79b`

Reproduce using the pinned PlatformIO Python environment. Let each verified
upload finish booting for eight seconds before the capture tool performs its
separate reset, to avoid consecutive USB startup resets.

```text
pio run -e guition_sample_playback_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M13_SAMPLE_STRESS_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m13.py docs/GUITION_M13_SAMPLE_STRESS_SERIAL.log
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M13_SYNTH_STRESS_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m13.py docs/GUITION_M13_SYNTH_STRESS_SERIAL.log --synth
pio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M13_NORMAL_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m13.py docs/GUITION_M13_NORMAL_SERIAL.log --normal
```

**M13 DSP/in-memory realtime/normal no-card qualification passes.** Physical
SD, audible and human UI validation remain pending, alongside the documented
rapid USB-reset startup limitation. Work stops here, before M14.
