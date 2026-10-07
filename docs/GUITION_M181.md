# M18.1 — quantized Performance Step Repeat

Accepted parent event repeat, with exactly x2/x4/x8 subdivisions of one captured
sixteenth. This is sequencer event replay through the existing voice path.
No audio recording buffer or independent transport was added.

## Baseline and changes

M18 was accepted and merged through PR #7 (`eb24dc1`), including implementation
`9dc8f58` and qualification `b31c4a5`. The comparison generator reads that actual
Git revision, checks unchanged sampler/slice dependencies and tests its codec.
M18.1 implementation `ee89850` adds the feature and tests; `3611740` refreshes
Repeat status after normal resume/stop. The qualification commit accompanies
this report and the raw captures. CI references are available on the milestone PR.

Changed source: `src/app/model.h` (bounded capture, scheduler, commands, owner,
hit geometry); `src/app/project.h` (runtime command classification);
`src/app_main.cpp` (voice routing, hold controls, publication, qualification);
`platformio.ini` (two hardware targets). Host tests, strict analyzers, workflow,
README and this report accompany the genuine serial logs.

## Capture and scheduling

`RepeatCapture` contains 16 unchanged 20-byte `TriggerEvent` values, a 16-bit
accepted SAMPLE/SYNTH routing mask and exact accepted count: 324 bytes.
`RepeatState` adds the next rational offset and requested/active/sub-index:
336 bytes. No heap, Pattern snapshot, PCM ownership or SliceBank lookup.

Start accepts only 2, 4 or 8 while playing. Latest pending rate wins. On the next
normal sixteenth onset the existing probability/mute/solo/lock/slice resolver
accepts parents, triggers them once, and stores them. Empty acceptance remains
silent. Normal pending ratchets are cancelled at that onset and no new ratchet
children are scheduled: Repeat replaces subdivision, never multiplies it.

The existing `phase`/`duration` sample clock is shared. Rational clock increments
are `BPM * 4` per audio sample. For N hits in captured swung duration D, sub-hit k
uses `ceil(D * k / N)`, k=1..N-1, from window origin. Initial parents are hit zero;
subsequent windows emit their own hit zero. Subtracting D preserves fractional
sample remainder across windows. Swing edits cannot change captured D. Tempo
commands retain existing clock increment semantics. Rate changes apply at the
next complete window. Stop before acceptance cancels without an audible change.

While held, no step, Pattern wrap, Chain entry/repeat, Override loop or Fill
completion advances, and no probability/edit RNG draw occurs. Stop during an
active window finishes its remaining hits, clears capture, then resumes the
following normal step once. Final-step release runs the existing wrap resolver
once, including length shrink and current numerical Chain edit semantics.

Priority is Repeat > Fill > Override > Arrangement. Fill requests stay pending;
Repeat inside Fill resumes its remaining steps. Override cancellation records
normally underneath Repeat and returns at its next legal Pattern boundary.

## Voice and project behavior

Repeated events retain all accepted velocity, pitch, volume, pan, wave, cutoff,
resonance, send, lock mask and resolved Playback values, plus accepted source
routing, despite later Track/Pattern/Slice edits. Only current runtime performance
mute/solo masks gate future sub-hits. Canonical acceptance remains frozen; masks
do not edit the capture or add previously skipped Tracks. Manual pads use the
independent audition queue and never enter capture.

Repeat emissions use the existing ratchet-like trigger path: same voice restarts,
no polyphonic stacking, no extra cross-track choke scans, and no Gate boundary
release per sub-hit. Normal Gate boundary handling resumes when Repeat exits.
Delay buffers/filter history remain intact and identical tones use the existing
coefficient cache. SAMPLE buffer replacement retains the existing sample lifecycle;
Repeat stores resolved events rather than a copy of PCM audio.

STOP and project LOAD/NEW reset every Repeat runtime field. SAVE snapshots only
canonical V2 data. Runtime commands are non-musical for dirty classification.
V2 remains 52,704 file bytes and 53,192 canonical State bytes, proven against M18.

## UI and release recovery

PERFORMANCE -> MIXER -> REPEAT -> PATTERNS cycles the subpages. Three large
240x152 buttons show x2 (1/2), x4 (1/4), x8 (1/8). Press requests that explicit rate;
release requests Stop. The active/pending/release/empty status includes audible
Pattern, frozen step and underlying Chain entry/repeat. A held gesture owns its
release independently of selection, page changes or finger drift. A new press is
refused until that ownership clears.

`RepeatGate` retains release_pending until Stop actually enters the bounded
command queue. An independent atomic physical touch-up publication covers a
release lost from the input queue. Project handoff also disarms the gesture.
UI mirrors requested/active/count, never publishes event arrays. Generation and
ack guards protect the four payload words; changes dirty only the three controls
and status. The UI Engine mirror grows with Engine; the `Ui` structure stays 116
bytes and publication grows from 20 to 24 bytes.

## Tests and hardware procedure

The allocation-guarded host test covers x2/x4/x8 exact offsets over 1,000 windows
for both swing parities at 50/60/75, plus non-divisible D=997/1999/3001 for 10,000
windows per rate; first-hit count; every release sub-index; rate changes;
16 immutable all-lock parents and source routing after edits; manual audition;
mixed probability/RNG paused reference; empty capture; final-step Chain repeat
accounting and edits; Fill/Override freezing and cancellation; pending Fill;
length shrink; Stop/load/new/Save V2; hit bounds and full-queue release recovery.
Actual M18 inactive-repeat comparison runs 7.2 million samples across PATTERN,
CHAIN, Override, Fill, mix, swing and all eight locks, and compares V2 bytes.

All inherited checks remain in CI. Hardware SAMPLE/SYNTH targets preserve the
first 2,068 dense blocks (16 voices, 240 BPM, 4 ratchets, all inherited locks,
filters and Delay) and the full first 60 seconds of previous scripted coverage.
The capture extends to 13,782 blocks (~80 s). After block 11,000 (~64 s),
a separate fixture captures
16 simultaneous parents at 240 BPM, Delay enabled, eight locks and SAMPLE slices.
x8 -> x2 -> x4 -> x8 changes and a pending Fill are exercised. Mute/solo suppression
occurs during x4 so the reported x8 voice range is the full 16-voice measurement.
The `full_blocks` counter verifies 240 BPM, live Delay, 16 capture events and all
applicable lock bits during measured x8 blocks (SAMPLE ignores Wave Lock,
SYNTH ignores Slice Lock, preserving M18 semantics). No per-hit Serial runs in audio.

Accepted logs are authentic native USB captures through the final `[M5 memory]` row.
Initial/interference/boot-only logs are preserved as diagnostics, excluded from
acceptance: initial fixture overlapped project/transport work; the next run
exposed ongoing legacy manual/choke scripts; boot-only captures encountered the
Windows USB disconnect. A further diagnostic retains the incorrect telemetry
expectation of mask 255: resolved SAMPLE masks are F7 and SYNTH masks 7F because
inapplicable Wave/Slice locks were already excluded by M18. A startup-missing capture lacks the initial SAMPLE fixture row; the pre-UI
SYNTH capture passed but predates the two-line status refresh. Both are kept
separately, and final SAMPLE/SYNTH logs include the current status fix.
The final fixture follows inherited scripts and isolates
the intentional repeat workload without weakening the earlier dense test.

## Measurements

Final raw logs: [SAMPLE](GUITION_M181_SAMPLE_STRESS_SERIAL.log),
[SYNTH](GUITION_M181_SYNTH_STRESS_SERIAL.log),
[NORMAL](GUITION_M181_NORMAL_SERIAL.log).

Deadline: 256/44100 = 5804.989 us; required max 4643.991 us for >=20% headroom.
All three final analyzers pass, with zero misses, failures, timeouts, rails or watchdogs.

| Measurement | SAMPLE | SYNTH | NORMAL |
|---|---:|---:|---:|
| Measured blocks | 13782 | 13782 | 10337 |
| p50 us | 2910 | 3319 | 1755 |
| p95 us | 3307 | 4091 | 1775 |
| p99 us | 3418 | 4192 | 1782 |
| Overall max us | 4617 | 4527 | 1938 |
| Inherited dense max us | 3664 | 4527 | inactive |
| Chain boundary max us | 3388 | 4310 | 0 |
| Performance boundary max us | 3534 | 4231 | 0 |
| Repeat max us | 4617 | 4145 | 0 |
| x8 max us | 4442 | 4145 | 0 |
| Snapshot max us | 3446 | 4155 | 0 |
| Apply max us | 1970 | 1811 | 0 |
| Peak PCM | 4010 | 7231 | 0 |
| Nonzero PCM values | 6946206 | 7054455 | 0 |
| Overall headroom | 20.46% | 22.02% | 66.61% |
| x8 headroom | 23.48% | 28.60% | inactive |

Each stress run reports 3 requests, 1 acceptance, 2 pre-accept cancellations;
8 x2 windows, 8 x4 windows, 99 x8 windows; 13,409 repeat-child hits and 12,672
x8 hits (including the 16 original hit-zero parents). Three rate changes, one
quantized release, zero empty captures, max capture 16, 15 overlay-suppressed hits.
Empty capture behavior is proven by host tests. UI press/release/edit counts are
1/1/10. Ordinary ratchet counters exclude Repeat emissions.

For both SAMPLE and SYNTH, x8 covers 1,067 measured blocks, active voices 16..16,
zero coefficient applications, 12,704 cache avoids and zero choke scans.
1,066 blocks verify the full 240-BPM/Delay/16-event/applicable-lock fixture;
one exit transition block remains in the worst-block accounting. Block-level
cache totals include neighboring normal events in transition blocks; x8 hits
are counted separately and exactly. The inherited first 2,068 blocks still
produce 1,024 ordinary ratchet hits/s; x8 produces 2,048 captured hits/s.

| Stable free memory (before = after), bytes | SAMPLE | SYNTH | NORMAL |
|---|---:|---:|---:|
| Internal free | 244660 | 269592 | 297220 |
| PSRAM free | 28616312 | 30779004 | 30779004 |
| Largest PSRAM block | 28311540 | 30408692 | 30408692 |

NORMAL has no Repeat requests, windows, hits, capture or UI repeat activity;
all runtime performance fields are neutral, no fixture runs, silence and stable
heaps. Normal firmware is restored on COM13. Every stress capture retains
three project fixtures, 51 snapshot and 312 apply blocks, zero fixture errors.

Runtime sizes: PerformanceState 352 bytes (M18 10, +342); Engine 52,616 bytes
(M18 52,224, +392, including 52-byte counters and alignment); TriggerEvent 20,
Track 116, Sample Voice 64, Pattern 3106, bank 49696, Chain 66 unchanged.
The longer stress-only render-time and sorting arrays add 27,560 bytes relative
to the 60-second qualification; normal firmware retains the old array length.
Heaps remained stable within every accepted capture.

## Acceptance and limitations

Local qualification passes all 21 PlatformIO environments and all 69 workflow
host/analyzer commands (66 retained/new checks plus the three final analyzers).
Remote CI rebuilds all 21 environments and retains every prior check; live
results are linked from the milestone PR. Merge is gated on successful CI.

**Yes:** P4SDM now performs quantized x2/x4/x8 step repeats from immutable
accepted events, pauses Pattern/Chain/Fill/Override progression, and resumes
sample-accurately at the following normal onset without persisting or dirtying
Repeat state or changing project V2 bytes.

Physical touch usability/rapid gestures, visual inspection, audible listening
and **PHYSICAL V2 PROJECT MIGRATION / CHAIN PERSISTENCE PENDING** remain; RAM codec/fixture
proof does not claim SD durability. No x8 fallback or voice-count reduction is
used. Scope stops at M18.1: no audio-buffer stutter, triplets, reverse, pitch
effects, scenes, macros or MIDI.
