# M15 — Per-Step Slice Lock

M14 accepted head `23049031b927ef4dc1fa4fd091e416e06bfdae48` is merged into
main at `5cae8e6`. M15 work is on `codex/m15-step-slice-lock` and stops here.

## Event contract

The uint8 lock mask now uses all eight bits: PITCH=0x01, VOLUME=0x02,
PAN=0x04, WAVE=0x08, CUTOFF=0x10, RESONANCE=0x20, DELAY SEND=0x40,
SLICE=0x80. StepLocks stores the requested zero-based slice as one byte,
clamped on explicit edits to 0..15. TriggerEvent retains its existing byte:
zero is unsliced, index+1 is sliced. It remains 20 bytes.

The canonical `resolve_event` accepts SAMPLE mask 0xF7 and SYNTH mask 0x7F.
For SAMPLE, an explicit Step Slice Lock selects the region even with SLICES
OFF; otherwise SLICES ON uses the Track active slice, and OFF uses Track
START/END. Explicit inspected-slice audition uses its supplied index with no
StepLocks. Track pad audition also supplies no locks. Neither audition reads
the selected step. Selection and stored requested lock values never change
when resolving playback.

The existing defensive SliceBank::index bounds the request against count,
including count=0. A request of 15 with count=4 resolves to 3 and returns to
15 after AUTO 16, with stored request untouched. Structural edits can change
the effective region of an out-of-range lock; deletion also shifts indices.
Track reverse, Gate/OneShot and choke remain in the captured Playback, with
all seven M12 parameters combining normally and source-inapplicable bits
excluded. Source switching retains lock data and reactivates it on SAMPLE.

Probability/mute/solo rejection precedes resolution and the trigger/choke
callback. Accepted parents capture the descriptor once; pending ratchets copy
that entire event. Later edits, unlocking, active selection changes, deletion,
reset and division cannot change remaining ratchets. Ratchets skip repeated
choke scans. Base-control refresh masks out SLICE before reconstructing live
controls and retains the accepted descriptor rather than reconstructing its
Slice Lock from telemetry.

Slice, SliceBank, waveform analysis/cache, PCM ownership, Transfer, sample
Voice and scheduler code remain unchanged. No new work is added to
Voice::next(), no SliceBank lookup occurs on ratchet callbacks, and no
realtime allocation is introduced.

## Pattern and UI

All existing whole-record transforms carry the new byte: Pattern COPY,
Duplicate, Track COPY, Rotate and Reverse. Euclidean, Randomize and Mutate
retain lock bytes. ClearStepLocks clears the new field and mask while
preserving trigger and StepMeta. Track/Pattern CLEAR retain lock metadata.

STEP -> LOCKS 1/3 -> LOCKS 2/3 -> SAMPLE LOCKS 3/3 adds a dedicated page.
Its display shows SLICE -- UNLOCKED, requested/available slice, or a requested
-> effective clamp. ENABLE LOCK/UNLOCK, PREV, NEXT and BACK use 56-pixel
vertical targets and discrete slice controls. Enabling initializes the current
effective Track selection even with SLICES OFF. SYNTH shows SAMPLE ONLY and
rejects Slice Lock edits; existing stored data is retained. Commands capture
Pattern, Track, Step and value explicitly, before asynchronous consumption.
The existing nonzero-mask cell marker includes bit 7. Existing controls retain
their dimensions, with global navigation below the new controls.

## Fixed memory

| Structure | M14 bytes | M15 bytes | Growth |
| --- | ---: | ---: | ---: |
| StepLocks (alignment 1) | 8 | 9 | 1 |
| Pattern | 2850 | 3106 | 256 |
| 16-Pattern bank | 45600 | 49696 | 4096 |
| Engine | 47968 | 52064 | 4096 |
| Two Engines | 95936 | 104128 | 8192 |
| Track | 116 | 116 | 0 |
| TriggerEvent / VoiceState | 20 / 24 | 20 / 24 | 0 |
| Sample Voice / sixteen voices | 64 / 1024 | 64 / 1024 | 0 |

No bit packing or per-trigger allocation. PCM remains 2097152 bytes in SAMPLE
stress; waveform caches and display/Delay buffers retain M14 dimensions.
Device heap and linked normal-memory results are recorded below.

## Host and equivalence validation

Seventeen C++ host suites pass, retaining all sixteen prior suites. The new
suite tests all 256 masks for every requested index 0..15, with SLICES both
ON and OFF and both sources; all exact boundaries/telemetry; selection
immutability; count shrink/regrowth and count=0; combined M12 controls and
Track descriptors; pending-ratchet structural edits/unlock; suppression;
normal choke versus ratchet scans; transforms, metadata retention and clear;
source switching; UI explicit intent and first/last button pixels. It also
checks 100000 lock resolution/sample Voice iterations without allocation.

Actual accepted M14 source is loaded through git show by
`tests/no_slice_lock_equivalence.py`. Across 3600000 samples, nine seed/swing
combinations, mixed SAMPLE/SYNTH and active/unsliced Tracks, varied seven-lock
masks, probability, ratchets, pattern changes, mute and solo, events, RNG,
phase, step, ratchet counts and probability rejection are identical with bit
7 absent. Retained scheduler/DSP/FX equivalence and historical capture checks
also pass. Historical analyzer defaults remain unchanged; exact M15 structure
sizes are passed through explicitly.

## Device qualification

The new guition_slice_lock_stress environment extends the complete M14 stress.
First 12 seconds retain 16/16 voices, 240 BPM, four ratchets, filters, seven
M12 locks, Delay and display/touch load. Every bank initially has sixteen
independent broad overlapping regions to retain that dense voice workload at
all varied indices. Deterministic requests use `(track*3 + step*5) & 15` and
mix locked and unlocked parents. Later sections retain active selection,
AUTO/ADD/DELETE/RESET and inherited workflows, add Slice Lock edits/unlocks
and SLICES OFF transitions, and exercise the new page. Aggregate telemetry
counts parents, requested/resolved ranges, clamping, applied Slice Lock value edits/unlocks/clears and UI edits without
per-event logging. Requested and resolved coverage bitmaps must both equal
0xFFFF, explicitly proving all sixteen indices were exercised. The prior
pre-mask capture passed inherited checks but did not yet provide that proof.

The fixed deadline remains 5804.989 us and minimum worst headroom 20%.
Strict M15 analyzers invoke all inherited M14 requirements. The initial and second coverage captures
are retained as rejected evidence: audio/heaps pass, but the first lacks
successful ADD/new-page edits and the second lacks new-page edits and enough
inherited playback UI edits. The routing capture exercises both but is
rejected because the dense counter required mask==0x7F and missed added bit
7. It now checks `(mask & 0x7F)==0x7F`, preserving the full seven-lock check. A broad older playback handler swallowed the
new IDs; its range is now bounded. The final script creates ADD capacity,
visits the new page, and explicitly completes inherited playback controls
without relaxing checks or dense work.

## Physical status and scope

**PHYSICAL M15 SLICE-LOCK VALIDATION PENDING**

COM13 reports NO CARD / MOUNT ERROR. Resident decoded PSRAM fixtures qualify
realtime behavior without SD. No audible chopped-break validation or human
visual/touch acceptance is claimed. No START/END, reverse or sample-selection
lock, transient detection, stretch, interpolation change, persistence, MIDI
or song mode is added.

**Yes:** one SAMPLE Track can sequence different slices step-by-step with
deterministic ratchets, without changing PCM ownership, the M13 renderer or
the scheduler hot path.

## Changed files and reproduction

- `src/app/model.h`: eighth lock, explicit UI intent, resolver and new geometry.
- `src/app/voice_state.h`: accepted descriptor retained during base edits.
- `src/app_main.cpp`: new page, bounded older handlers, aggregate telemetry and stress.
- `platformio.ini`: thirteenth environment, all previous environments retained.
- `tests/slice_lock_test.cpp`, `tests/no_slice_lock_equivalence.cpp/.py`: new suites.
- `tests/analyze_m15.py`: strict inherited plus M15 qualification.
- `tests/analyze_m12.py`, `analyze_m13.py`, `analyze_m14.py`: explicit sizes with historical defaults.
- `tests/tone_locks_test.cpp`, `track_tone_test.cpp`: updated current structure-size assertions.
- `.github/workflows/guition.yml`: every M3-M14 check retained, M15 added.
- `README.md`, this report and genuine M15 serial logs: workflow and evidence.

Windows reproduction uses existing Python 3.11 PlatformIO 6.1.18 at
`C:\.platformio\penv\Scripts\python.exe`, with PYTHONUTF8=1 and
PYTHONIOENCODING=utf-8. Wait at least eight seconds after verified upload
before issuing the capture's separate reset.

```text
python -m platformio run -e guition_slice_lock_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M15_SAMPLE_STRESS_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m15.py docs/GUITION_M15_SAMPLE_STRESS_SERIAL.log
python -m platformio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M15_SYNTH_STRESS_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m15.py docs/GUITION_M15_SYNTH_STRESS_SERIAL.log --synth
python -m platformio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M15_NORMAL_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m15.py docs/GUITION_M15_NORMAL_SERIAL.log --normal
```

An initial all-environment rebuild exhausted the host C: drive. PlatformIO's
built-in `system prune --cache --force` reclaimed disposable cache data;
source/evidence were preserved and builds were retried. This was a host build
failure and does not count as accepted qualification evidence.

## Final SAMPLE result

Genuine final SAMPLE capture: 10337 blocks / 60.006 seconds;
p50/p95/p99/max = 3053/3308/3378/3549 us, 38.86% worst headroom.
The maximum is 17 us below accepted M14's 3566 us. Zero misses, I2S
failures/timeouts, rails, command rejects or heap loss. First 2068 blocks keep
16/16 sample voices, 240 BPM, four ratchets and 1024 hits/second, with all
seven existing lock bits present even when Slice Lock is added.

Accepted SAMPLE parents: 9980 locked, 2764 unlocked sliced, 987 unsliced.
Requested/resolved min/max are 0/15; both coverage masks are 65535 (0xFFFF),
proving every index. There are 2483 clamped resolutions, 231 applied Slice
Lock edits/unlocks/clears and 44 new-page UI edits. The run also applies 46
active selections, 115 AUTO divisions, 23 ADDs, 22 DELETEs, 22 RESETs and
22 auditions. Command/slice-edit blocks max at 3458 us. No per-event logging.

SAMPLE internal heap remains 277188/277188; PSRAM remains
28722816/28722816, largest 28311540/28311540. No pool, waveform or renderer
allocation changes. The retained M14 memory structure sizes are checked by
the inherited analyzers with only Pattern/Engine/StepLocks expectations
updated. New telemetry and UI arrays are small fixed storage.

## Commits and CI

Implementation: `1d6ab29`. UI routing/coverage corrections: `955ef3d`,
`aa11e39`. Dense seven-lock telemetry correction: `4be76b2`. Complete index
coverage and edit counters: `00deac3`. A final evidence commit includes this
report, README and genuine captures; its hash is reported in the handoff.

All thirteen firmware environments build locally, all seventeen host suites
pass, and all seven current/retained equivalence or FX scripts pass. Every
historical M3-M14 workflow check is retained and passes locally. M15 adds its
host suite, actual-M14 equivalence and SAMPLE/SYNTH/normal analyzers without
physical SD dependencies. The final uploaded SAMPLE/SYNTH/normal firmware is
built from `00deac3`. Hosted results are reported against the evidence head
in the handoff and available in
[branch CI](https://github.com/ovelhaaa/P4SDM/actions?query=branch%3Acodex%2Fm15-step-slice-lock).
M14 merged main CI is green:
[run 37564886006](https://github.com/ovelhaaa/P4SDM/actions/runs/37564886006).

## Final SYNTH result

Genuine SYNTH capture: 10337 blocks / 60.006 seconds; p50/p95/p99/max =
3360/4022/4112/4383 us, 24.50% worst headroom. All sixteen synth voices
remain active; zero misses, I2S failures/timeouts, rails or command rejects.
All Slice Lock counters, coverage masks, sample/slice triggers, waveform
builds and sample UI counters are zero. Internal heap is 302116/302116;
PSRAM is 30885508/30885508, largest 30408692/30408692. Its strict M15 and
all inherited analyzers pass. The maximum is 3 us above accepted M14's
4380 us, with no material regression.

## Restored normal firmware and final qualification

Normal guition_app is restored to COM13 with flasher hash verification.
Its genuine no-card capture has 10337 blocks / 60.006 seconds;
p50/p95/p99/max = 1749/1769/1776/1877 us,
67.67% worst headroom. Zero deadline misses, I2S failures/timeouts,
command rejects, rails, nonzero PCM, peak, sample voices, slice/locked
triggers, waveform analysis or qualification residue. UI submits/completes
one initial frame. All strict M15 and inherited normal analyzers pass.

Internal heap is 302148/302148; PSRAM free is 30885508/30885508 and largest
30408692/30408692. Normal free internal memory is 8304 bytes below accepted
M14, primarily the fixed 8192-byte increase for two Engines, with small fixed
UI/telemetry overhead. Normal PSRAM free/largest are unchanged. SYNTH and
SAMPLE remain stable as reported above. No per-trigger heap growth occurs.

PlatformIO's linked normal RAM report is 75424 bytes (23.0% of its reported
327680-byte board budget), Flash 658436 bytes. These linker figures are
reported separately from complete structure sizes and actual runtime free
heap; they are not a replacement for either measurement. Display buffers
remain 2304000 PSRAM bytes, Delay 352800 bytes, with no normal fixture PCM
or resident waveform allocations.

All three final genuine captures pass. SAMPLE max 3549 us (38.86%), SYNTH
4383 us (24.50%) and normal 1877 us (67.67%) retain the fixed
5804.989-us deadline and >=20% threshold. Physical SD/audible/human touch QA
remains pending; software, host and in-memory realtime qualification passes.

Verified-upload firmware SHA256:

- `guition_slice_lock_stress`: `5847a1d0131e9817a8284bc880c5971a3af14205f6976fd91843371c0e3dc57a`

- `guition_app_stress`: `dc0e221e04db8ceb4b32379d40a2a4fe72e1dfb57aec8025c6321bebe26007d6`

- `guition_app`: `0433a7d908738b5e009c6206263e75faeaaffe0c14f9386a20d40703fccaf9bd`
