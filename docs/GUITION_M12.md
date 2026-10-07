# M12 — Tone Parameter Locks

M12 extends the accepted M10 event model to cutoff, resonance and continuous
Delay send, on merged M11 main (`9ea1374`; implementation baseline
`4afaa51cec574594b35d1f8bfee7938cced6de31`). Work stops after M12.

## Ownership and resolved tone

`resolve_event()` is the single lock-resolution point for all seven controls.
A set mask bit selects the stored step value; an absent bit selects Track base.
Accepted parents store the complete resolved TriggerEvent in Pending; ratchets
emit that same snapshot. Step/Track edits after acceptance do not rewrite it.
Probability rejects and mute/solo suppression never reach the trigger callback.
An unlocked following parent, pattern boundary or manual pad resolves base
values independently. Pads ignore selected-step locks, mute and solo gates.

`VoiceState.event` retains the accepted snapshot. At trigger, the app stores it,
applies its effective filter/send, then triggers SYNTH or resident SAMPLE.
Live base commands reconstruct only unlocked controls through `event_locks()`
and `resolve_event()`. Locked cutoff/resonance/send resist corresponding base
edits; unlocked parameters remain live. An accepted ratchet can reapply its
original captured values after an intervening live edit, matching the existing
M10 snapshot philosophy. Track values are never overwritten, and there is no
restore timer/system. A new manual/unlocked event replaces all active overrides.
Source changes and sample replacement clear active voice ownership and retain
Pattern lock data; their existing filter-history resets remain lifecycle-only.

**Compatibility applies to the final resolved pair, not lock presence:**
M11's exact `p4tone::cutoff()` / `p4tone::resonance()` remain the only coefficient
mapping. Resolved 0/0, including two explicit zero locks, means legacy f=255,
q=255. Cutoff lock 64 with unlocked base resonance 0 resolves to 64/0 and q=0.
Cutoff 0 with resonance lock 64 resolves to q=floor(64*160/127)=80.
Every other pair uses edited-tone q<=160. No lock-only mapping exists.

SYNTH and resident SAMPLE both use the existing post-source FILTROS[track].
Coefficient edits retain history. `VoiceState` caches the last applied canonical
pair, so identical parent/ratchet pairs skip both setters. A live edit to an
unlocked half recomputes the whole effective pair, preserving 0/0 semantics.
DelaySend-only commands pass `tone=false`, so they never touch filter setters.

`voice_delay_send[16]` mirrors the precomputed effective sends at trigger/update
boundaries. The per-sample hook performs a byte-array load and bounded int32
send/127 scaling, with full send as exact identity. It contains no mask checks,
Pattern/Track lookup, resolution, coefficient mapping, allocation or int64
arithmetic. The separate `resolved_delay_send()` entry trusts the 0..127
boundary invariant; the original clamped helper remains for generic callers.
Routing remains post-filter/post-pan stereo. Zero send leaves dry audio and
existing Delay tails intact; global OFF still disables Delay processing.

## Layout and memory

All fields are byte-sized, fixed-storage and trivially copyable. Values are
clamped by commands, with explicit mask presence even when equal to base.

| Structure | Byte offsets / fields |
| --- | --- |
| StepLocks | 0 mask; 1 pitch; 2 volume; 3 signed pan; 4 wave; 5 filter_cutoff; 6 filter_resonance; 7 delay_send |
| TriggerEvent | 0 track; 1 velocity; 2 pitch; 3 volume; 4 signed pan; 5 wave; 6 locked_mask; 7 filter_cutoff; 8 filter_resonance; 9 delay_send |
| Lock bits | pitch=1; volume=2; pan=4; wave=8; cutoff=16; resonance=32; send=64 |

| Storage | M11 bytes | M12 bytes | Growth |
| --- | ---: | ---: | ---: |
| StepLocks | 5 | 8 | 3 |
| TriggerEvent | 7 | 10 | 3 |
| Pattern | 2082 | 2850 | 768 |
| 16-pattern bank | 33312 | 45600 | 12288 |
| Engine | 34248 | 46592 | 12344 |
| Audio + UI Engine copies | 68496 | 93184 | 24688 |
| Audio active events/cache/send array | 112 | 240 | 128 |
| Combined Engine copies + active state | 68608 | 93424 | 24816 |

VoiceState is 14 bytes, including a 10-byte event, cached cutoff/resonance/send
and a validity flag; sixteen states use 224 bytes. The contiguous send mirror
adds 16 bytes. Command remains 12 bytes. Remaining internal heap and PSRAM are
reported from real captures below; extra telemetry/UI counters also consume
small fixed storage beyond the above ownership structures.

Fields remain suitable for future serialization. Active snapshots, effective
send, coefficient caches, filter buffers and Delay tails are runtime-only.
No persistence is implemented.

## LOCKS UI and transforms

LOCKS 1/2 retains PITCH, VOLUME, PAN and WAVE. NEXT opens LOCKS 2/2 with CUTOFF,
RESONANCE and DELAY SEND, plus CLEAR ALL LOCKS, BACK to 1/2 and BACK TO STEP.
Rows use 520x56 value targets and 216x56 enable/unlock targets; footer targets
are 240x52. Footers sit at y=362, clear of the global navigation beginning at
420. Host geometry tests check all corners and overlapping controls.

Enabling starts from current Track base. Disabled rows show `-- UNLOCKED`;
active values show `LOCKED`; cutoff zero shows `OPEN LOCKED`, and its right
slider endpoint is OPEN. Value drags dirty one value widget. Presence changes
also dirty the enable/unlock widget and the grid marker when any-lock presence
changes. Clear redraws the visible lock rows; page navigation redraws its page.
Any of the seven bits produces the same readable step lock marker.

Both clear buttons clear all seven locks without altering triggers/StepMeta.
Rotate/reverse/copy-track/copy-pattern/duplicate already copy complete records
and therefore move the new fields. Euclidean changes triggers only;
Randomize/Mutate preserve locks. Source changes retain stored locks.

## Host and CI validation

Twelve C++ host suites pass, retaining all eleven M3–M11 suites. The new
`tone_locks_test` covers all 128 masks on SYNTH/SAMPLE, mixed-pair mapping,
active locked/unlocked base edits, send-only setter exclusion, stereo send
scaling, full/zero send, following unlocked events, edited-parent ratchets,
probability/mute/solo rejection, manual audition, pattern boundaries, transforms,
clear/source semantics, clamps/addressed intent, UI enable values/corners,
100000 rapid coefficient changes checked against an int64 oracle for int32
intermediate safety/history continuity, and 100000 allocation-free operations.

`no_tone_lock_equivalence.py` obtains the actual M11 model through `git show`:
3.6 million sample positions match all original event fields, transport, RNG,
probability, ratchets, swing and switching. New resolved tone/send fields equal
M11 Track values when tone lock bits are absent. As with existing M10 snapshots,
accepted M12 ratchets capture tone as well; the equivalence fixture holds Track
tone stable across a pending parent, while separate tests cover captured edits.

`tone_lock_routing_equivalence.py` obtains the actual M11 filter and mapping/send
helpers through `git show`. Across 132352 deterministic oscillator-like/resident
PCM frames, varying unlocked Track tone/send produces bit-exact filters, dry,
master, stereo bus and wet outputs. Zero-send dry and Delay tails remain intact.
This is a host DSP routing comparison, not a physical DAC/sample capture.
The retained actual-M9 no-lock, actual-M10 scheduler/default DSP comparisons and
all eight original FX digests pass. Historical M4/M4.1/M5/M6/M6.1/M7/M8/M9/M10/M11
capture analyzers pass their retained evidence; historical recorded failures
remain failures in those reports.

CI retains every previous workflow step and adds the M12 host test, actual-M11
scheduler/routing equivalence, and stress/normal analyzer invocations. M10/M11
analyzers keep their historical size defaults and accept an explicit Pattern
size for inherited M12 checks. No SD dependency is introduced. All ten pinned
firmware environments and final capture analyzers are qualified locally below.
Hosted GitHub CI has not been run; no push or merge is claimed.

## Hardware qualification

The first genuine candidate (`GUITION_M12_HEADROOM_FAILED_SERIAL.log`) had
max=4704 us, 18.97% headroom, zero misses and stable heaps. It fails the unchanged
20% threshold and remains preserved. Its cache already avoided 32545 setter
applications; coefficient-change blocks peaked at 4630 us. Optimization then
made the hot send load a contiguous byte-array access, removed its redundant
clamp under the established boundary invariant, and inlined boundary helpers.
An intermediate genuine run reached max=4428 us, 23.72% headroom. The workload,
priority, display quality, deadline and features were not reduced.

Final genuine evidence: `GUITION_M12_STRESS_SERIAL.log`. `analyze_m12.py`
passes all nested M11/M10/M9/M8/M5 assertions. The first 12 seconds run sixteen
voices/filters, all seven lock bits, four ratchets, 240 BPM, Delay, display/touch
and transforms. Neighboring steps alternate cutoff 20/100/40/120, resonance
10/100/40/127 and send 0/127/32/96. Later commands enable/disable/edit locks,
change base values, switch patterns and transform locked records.

| Measurement | Result |
| --- | --- |
| Audio blocks / nominal duration | 10337 / 60.006 seconds |
| Render/control p50 / p95 / p99 / max | 3349 / 4032 / 4123 / 4353 us |
| Budget / worst headroom | 5804.989 us / 25.01% |
| Dense seven-lock blocks / worst | 2068 / 4353 us |
| Dense hit rate | 1024 hits/second |
| Coefficient-change blocks / worst | 1016 / 4209 us |
| Tone-lock-edit consumed blocks / worst | 211 / 4209 us |
| Transform blocks / dense transforms / worst | 150 / 123 / 4353 us |
| Cutoff / resonance / send locked parents | 13230 / 13190 / 13233 |
| Coefficient applications / avoided | 13882 / 39880 |
| Effective send changes / explicit tone-lock commands | 13839 / 147 |
| Tone-lock UI entries / edits / maximum widgets / edit full redraws | 9 / 58 / 6 / 0 |
| Parents / probability passed / skipped / ratchet hits | 14926 / 13669 / 1257 / 38603 |
| Accepted locked / unlocked parents | 13532 / 137 |
| Active voices min / max | 16 / 16 |
| PCM peak / nonzero values | 6409 / 5290801 |
| Deadline misses / write failures / timeouts / command rejects / rails | 0 / 0 / 0 / 0 / 0 |
| UI seconds / submitted / completed | 63.085 / 1359 / 1359 |
| Internal heap before / after | 315940 / 315940 bytes |
| PSRAM free before / after | 30885508 / 30885508 bytes |
| Largest PSRAM block before / after | 30408692 / 30408692 bytes |

Compared with M11 stress internal free=340920, M12 uses another 24980 bytes of
fixed internal storage, including the ownership growth and new diagnostics/UI.
PSRAM free and largest block are unchanged. The capture records no heap loss.
Stress firmware SHA256:
`b5a04d8e8e88432aa6c7dbcde83dbfd60c6e428254d9174ed948e24bd857c710`.

Every one of the ten firmware environments compiled locally under pinned
pioarduino 55.03.36-1. The twelve host suites, all retained comparisons and
historical analyzers also pass locally. Hosted CI status remains unverified.

Normal `guition_app` is restored to COM13 after stress with flash hash
verification. `GUITION_M12_NORMAL_SERIAL.log` passes `analyze_m12.py --normal`.
The separate 10337-block / 60.006-second idle run has zero active voices,
nonzero PCM, peak, parents, ratchets, tone/lock edits, coefficient applications,
effective-send changes, stale lock/tone residue, rails, misses, write failures,
timeouts and rejects. Render p50/p95/p99/max is 1747/1767/1774/1881 us.
Internal free remains 315988 bytes; PSRAM free/largest remain
30885508/30408692 bytes. Display, touch and audio initialize successfully;
the expected no-card status remains. Normal firmware SHA256:
`c256ee65897f38b19b8a657a19dff523793672b5cd761c89caadd25e115bb901`.

Reproduce with the existing pinned PlatformIO Python environment (Windows sets
PYTHONIOENCODING=utf-8 for uploads):

```text
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M12_STRESS_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m12.py docs/GUITION_M12_STRESS_SERIAL.log
pio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M12_NORMAL_SERIAL.log --seconds 95 --until "[M5 memory]"
python tests/analyze_m12.py docs/GUITION_M12_NORMAL_SERIAL.log --normal
```

## Changed files and commits

Implementation commit: `f2f9e12` — Implement M12 accepted-parent tone parameter locks.
The subsequent qualification commit records this report and the genuine final
stress, normal and preserved failed captures.

- `src/app/model.h`: lock/event storage, commands, seven parent counters and UI geometry.
- `src/app/voice_state.h`: accepted snapshot reconstruction and coefficient/send cache.
- `src/app_main.cpp`: shared trigger/base boundaries, effective send array, LOCKS pages,
  dirty rendering, dense variation, live edits, transforms and capture telemetry.
- `src/engine/track_tone.h`: bounded hot send entry, unchanged M11 mapping.
- `tests/tone_locks_test.cpp`: architecture, mapping, lifetime, numerical and UI tests.
- `tests/no_tone_lock_equivalence.{cpp,py}`: actual-M11 scheduler comparison.
- `tests/tone_lock_routing_equivalence.{cpp,py}`: actual-M11 PCM/routing comparison.
- `tests/track_tone_test.cpp`: retained M11 assertions with new structure sizes.
- `tests/analyze_m10.py`, `tests/analyze_m11.py`: explicit inherited Pattern-size option.
- `tests/analyze_m12.py`: strict M12 stress/normal acceptance, unchanged 20% budget.
- `.github/workflows/guition.yml`: all prior checks plus M12 checks.
- `README.md`, this report and genuine M12 serial logs: behavior, limits and evidence.

## Physical limitations and future architecture

**PHYSICAL SAMPLE TONE-LOCK VALIDATION PENDING.** COM13's device still has no
usable SD card. Resident PCM tone/send routing is covered by the shared
architecture and host fixtures. No audible listening, human touch/visual QA or
physical resident SAMPLE qualification is claimed. The page-heavy script also
makes no strict 30-FPS guarantee.

**Yes:** the accepted-parent parameter-lock architecture can support future
FX/sample locks without redesigning Track ownership or the audio hot path.
Future fields can extend the fixed mask/value record, canonical resolver,
immutable accepted snapshot and boundary-applied effective state. Source-specific
parameters can be applied at trigger/control boundaries; parameter-lock lookup
still need not enter per-sample DSP. Their individual mapping, lifecycle, memory
and timing would need their own qualification. No other FX/sample locks, ramps,
microtiming, MIDI, song mode or project persistence are implemented in M12.
