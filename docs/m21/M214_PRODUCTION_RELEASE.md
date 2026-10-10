# M21.4 — Linear production proposal and musical acceptance

**PARTIAL — technical qualification incomplete. Normal production remains
Nearest. Human musical acceptance is PENDING. No M22.**

## 1. Baseline and preserved fallback

Baseline/main/reference merge: `1c4c926326cc959cad711a74e5c058e08a718f2a`.
`origin/main` was fetched and matched. Initial working tree contained only the
untracked user file `docs/m21/error.png`; it was preserved. Work uses
`codex/m214-linear-release`. The inherited 96 host checks passed. Baseline
production was rebuilt before the proposal and archived with its ELF, BIN, MAP,
bootloader, partitions, SDK/config/source hashes and manifest in
`.pio/m214-artifacts/baseline/production`. Historical M21.3 evidence is retained.

## 2. Frozen interpolation

`guition_app_linear_candidate` proposes PCM16, unchanged Q16 position/increment,
`P4SDM_INTERPOLATION=1`, `P4SDM_LINEAR_32BIT=1`,
`P4SDM_LINEAR_MAGNITUDE=1`, `P4SDM_PCM_READ_CACHE=0`,
`P4SDM_PCM_PLACEMENT=1`. Unsigned magnitude rounds ties away from zero exactly
as accepted Linear64, including INT32_MIN. Kernel arithmetic is unchanged.
Linear64, signed Linear32, Nearest, Hermite and historical fixture controls
remain available. There is no runtime quality switching.

## 3. Placement decision

The proposal uses independent allocations, 64-byte alignment and Track*64-byte
offsets. At most 1,023 requested padding bytes per sample; sixteen residents
request 8,688 total padding bytes, plus up to 1,023 for one staging allocation.
The owning metadata adds 12 bytes on the 32-bit target. Heap bucket rounding
and allocator headers are additional: these bounds do not describe total heap
consumption. The 4 MiB reserve uses actual free PSRAM after allocation.

M21.3 measured same-size genuine WAV interleaved lookup at 2,714.09 vs 141.25 us
per 4,096 reads for ordinary vs staggered placement. Its complete magnitude B
renderer maxima were 4,105 vs 4,099 us, a difference of only 6 us (0.15%). This
supports reducing pathological placement risk, not a universal 94.8% renderer
speedup. Fresh M21.4 SD layout comparisons are required separately below.

## 4. Why direct PCM

The selected application evidence in M21.3 favored direct independent PCM
reads. Its staggered cached magnitude B maximum was 5,017 us, exceeding the
4,643 us reserve limit, versus 4,099 us direct. The 32-frame cache is retained
as a historical diagnostic; it is absent from the candidate's audio path.

## 5. Ownership audit

The shared browser/Project loader owns `LoadedSample::pcm.base`; audio and
waveform processing use `pcm.data`. `Sample::allocation` remains a byte count.
`P4SDM_PCM_PLACEMENT` replaces the loader's milestone-specific flag;
`P4SDM_M213_LAYOUT` maps to it only for historical build compatibility. Conflicting
and invalid policies fail compilation. Policy 0 retains the legacy loader.

| Path | Ownership/result |
|---|---|
| Browser and Project Load | One storage task validates PCM16 WAV, allocates base/view and decodes; same `load_name` path |
| Replacement | New owner is staged alongside old; Transfer acquire/release acknowledgement stops old Voice before storage retires old base |
| Allocation or metadata failure | No publication; resident unchanged; host owner-failure tests and diagnostic allocator fault injection |
| Invalid/truncated WAV | Validation precedes allocation; physical generated invalid files and inherited parser fixtures |
| Incomplete read/seek | Only staged base is freed; payload restored; storage short-read injection exercises actual cleanup |
| Missing/unmounted SD | No new owner; resident preserved; unmount injection is explicitly simulated absence, not physical card removal |
| Repeated loads and fragmented PSRAM | Bounded padded request must fit actual pre-allocation largest block/free budget; actual post-load free/largest values recorded and checked |
| Project activation | Audio detaches/stops first; UI preview removal precedes storage retirement; missing references retain names and report missing Tracks |
| Failed Project Load | Validated staging must succeed before activation; existing project remains; CRC/generation and missing-project tests |

The accepted acknowledgement/preview/retirement protocol is unchanged. No
allocation, deallocation, mutex or new synchronization enters the audio callback.
`src/app_main.cpp`, Engine, Voice transport, pitch table, Project codec, slices,
transients and SYNTH/filter/Delay sources are unchanged. No dead diagnostic
reference kernel was removed merely to reduce code count.

## 6. Scenario A equivalence — open technical gate

The original wall-clock A scenario and strict M20/M19/M18.1 analyzers remain
untouched. Its fresh timing run passes those analyzers. It is **not declared
byte-identical** to the Nearest wall-clock run.

`tests/m214_replay.cpp` adds a supplemental block-scheduled control-family
replay: 13,782 blocks, 4,717 commands, three real Project V2 snapshot/encode/
decode/apply transactions, 92,864 events, 133 Chain loops and 21,504 x8 hits.
Nearest, Linear64 and direct magnitude Linear32 share hash `3236740295` and
per-sample transport/fade/route state. This starts from the deterministic mixed
fixture and does **not** reproduce every original A UI/project/transient command.
It strengthens regression coverage but does not close the requested complete
original-A replay gap. That requirement remains PENDING and prevents TECHNICAL
PASS or production promotion.

## 7. B/C equivalence and mathematical regressions

The exact inherited B/C event schedules are retained. The host matrix now
explicitly includes direct cache-free magnitude Linear32. Expected short-run
digests remain B=`978631659`, C=`3904423500`, with 126,480 events, 230 Chain
cycles, 7,168 x8 hits and all sixteen continuously active voices.

The final flags pass the source-accurate original Nearest/Voice regression:
65,536 fractional phases, PCM rails, unity, all MIDI pitches, 1–4-frame and
arbitrary bounded regions, forward/reverse, fades, Gate/OneShot, release,
retrigger, duration, replacement and allocation guards. Magnitude separately
matches Linear64 for all rail/control phase combinations and one million random
tuples. The 49 complete component listening renders also match Linear64 PCM
exactly. Voice remains 56 bytes on the target / 64 on this 64-bit host.

## 8. Objective audio comparison and limits

All seven source types are **synthetic**, including a vowel/formant vocal proxy.
There are 49 pairs: kick, snare, hi-hat, melodic instrument proxy, sustained
tone, vocal proxy and percussion loop at -12/-7/-1/0/+1/+7/+12 semitones.
Three-second stereo renders use actual Voice/Q16/fades, velocity 100, accepted
default filter and actual Delay (time 12,000, feedback 120, input 160, send 32),
wet gain 1/4 and common output gain 1/2. Note onsets, reverse Gate slice and
release positions are identical. No independent normalization is applied.

Full-file integrated RMS dBFS and PCM sample peak are recorded in
`m214_audio_metrics.json`; they are not LUFS or reconstructed true peak. Unity
pairs have identical PCM. At +7 semitones, B-minus-A RMS differences include
kick ~0 dB, snare -1.507 dB, hi-hat -3.337 dB, melody -0.006 dB and vocal proxy
-0.050 dB. These illustrate real amplitude/texture differences, not preference.

Linear reduces nearest stepping and changes high-frequency/transient texture;
tonal reproduction and coloration must be judged by listening. **Linear does
not solve upward-pitch aliasing.** No antialias filter was introduced.

## 9. Listening artifact locations

Run `python tests/m214_listening.py`. Local outputs:
`.pio/m214-listening/m214-listening.zip`, individual neutral `*_A.wav` and
`*_B.wav`, `README.md` checklist and `manifest.json` exact processing/levels.
A=Nearest; B=optimized magnitude Linear32. CI uploads the package and firmware
under `m214-proposal-firmware-and-listening`, with 30-day retention.

## 10. Human listening

**PENDING.** Explicit listening feedback was requested. No human listening,
instrument recording or subjective approval is inferred from numerical tests,
CI or synthetic proxies. Feedback should identify source, pitch, preference,
transient/timbre defects and playback system. Keep a fixed listening volume.

## 11–13. Fresh physical A/B/C and endurance

The dedicated COM13 board identifies ESP32-P4 v1.3. Deadline is
5,804.988662 us at 44,100 Hz / 256 frames; the integer maximum must be <=4,643 us
for >=20% reserve. Use maximum, not p99, for qualification. Every run uses its
own matching archived ELF/BIN/MAP and new raw log; no M21.3 run substitutes for
a fresh final-flags run.

| Run | p50 | p95 | p99 | max us | headroom | status |
|---|---:|---:|---:|---:|---:|---|
| A, original workload | 2847 | 3699 | 3815 | 4121 | 29.01% | strict timing/application PASS; complete immutable equivalence PENDING |
| B, full fractional regions | 3764 | 3980 | 4024 | 4099 | 29.39% | timing/events PASS |
| C, mixed fractional | see `m214_results.json` | | | 4120 | 29.03% | timing/events PASS |
| SAMPLE, >=10 minutes | PENDING | | | | | capture underway |
| SYNTH, extended final flags | PENDING | | | | | fresh capture required |

The machine-readable results contain voice counts, Chain/x8 counts, exact
timing quantiles, misses, I2S failures/timeouts, faults, heap/largest-block
snapshots and PCM monitoring. A is not a constant sixteen-voice workload.
B/C require every Track active for all accepted frames. Exact short-run internal
heap can increase by 528 bytes during cleanup; increases are reported as such.

## 14. Physical SD and Project tests

Fresh ordinary and staggered layout consoles use the candidate's exact DSP
flags and the mounted card's existing generated `m19p_*.wav` fixtures. Only the
reserved `M214_LAYOUT_QUAL` project/backup and reversible diagnostic WAV rename
are writable. Coverage includes mono/stereo downmix, short/large/equal/mixed
WAVs, reversed repeated replacement, deliberate fragmentation/pressure,
Reverse, Slice Locks, Gate/choke, x8 Repeat, Analyze/Apply/Cancel, Chain,
Project Save/Load, invalid/truncated/missing WAV, simulated unmounted SD,
short-read and allocator failure, generation/CRC fallback and missing references.
No touch gesture or physical card-removal approval is inferred from this console.
Fresh results remain PENDING until the entire host procedure and recorded
physical timing/reserve validate.

## 15. Memory and PCM integrity

Project V2 is exactly **52,704 bytes**, with no persisted interpolation or PCM
placement fields. Inherited V1 migration, old/new V2 saves, semantic CRC-correct
rejection, generation recovery and truncation tests pass. New production config
checks freeze Voice size and accepted scheduler/codec/transport sources. The
extended tests require periodic PCM digests and stable free/largest/internal
snapshots; required sustained evidence is not inferred from short captures.

## 16. Reset and brownout observations

No recurrence is visible in completed A/B/C captures. Native USB reconnect
can omit early ROM reset lines; their absence is not an electrical diagnosis.
The final SD console adds read-only `POWER` queries of `esp_reset_reason()` and
the brownout enum before/after the workload. Protection is unchanged. The
earlier alignment-only M21.3 brownout remains unexplained; no power measurement
or software attribution is claimed. A recurrence blocks stability acceptance.

## 17. Remote CI

PENDING final remote run. Retain all inherited checks and add final config,
candidate interpolation/source output, direct B/C traces, owner tests,
supplemental A replay, fresh physical capture validation, Linux ASan/UBSan,
standalone candidate/fallback builds, and reproducible listening outputs.
Green CI cannot satisfy the open complete-A or human listening gate.

## 18. Proposal SHA and reproduction

Source/proposal commit and final remote run will be recorded after verification.
Exact artifact identities are in `m214_artifacts.json`; its per-build manifest
records build-time baseline SHA, dirty source hashes and config/SDK hashes.
These pre-commit builds must not be mislabeled as clean builds of that baseline.
`m214_inventory.json` links raw captures to exact firmware hashes.
`.pio/m214-release-evidence.zip` packages matching physical firmware locally;
CI rebuilt firmware is separately labeled and is not claimed hash-identical to
the physically captured archives.

Commands: `python tests/m214_device.py build candidate fallback A B C
SAMPLE_sustained SYNTH_sustained`, then `python tests/m214_device.py capture A B C
SAMPLE_sustained SYNTH_sustained`. Finish each build operation before capture.
Prepare/build/archive SD consoles via `tests/m214_sd_prepare.py`, then
`tests/m214_sd_device.py 0 1`; every physical driver restores the preserved
production image in `finally`. Re-run `tests/analyze_m214.py --require-captures`.
`--require-technical` deliberately fails while the complete-A gate is open.

## 19. Nearest fallback

`pio run -e guition_app_nearest_legacy` is independently buildable with explicit
Nearest/cache-off/ordinary-placement flags. Normal `guition_app` also remains
Nearest. To restore the exact original image, use the hash-verified
`.pio/m214-artifacts/baseline/production` archive, not a rebuilt image relabeled
as its match. Do not flash old partition images independently of their manifest.

## 20. Acceptance answers and promotion gate

1. Mathematical, transport, B/C scheduling and Project V2 regression evidence
   passes; the full original-A deterministic replay is incomplete.
2. Bounded ownership tests pass; final real SD recovery/fragmentation evidence
   must finish before making the fresh physical safety claim.
3. Fresh A/B/C meet the reserve; fresh extended SAMPLE/SYNTH are still required.
4. Fair reproducible synthetic pairs exist; worthwhile musical improvement and
   acceptable transients require explicit human feedback.
5. Linear is a reproducible separate proposal with a clean Nearest fallback;
   promotion is blocked. This is **PARTIAL**, not TECHNICAL PASS or production
   approval. No antialiasing, time-stretch, Hermite optimization, UI redesign,
   persistence change or M22 work is included.
