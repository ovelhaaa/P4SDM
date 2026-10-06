# M9 — Pattern tools and generative editing

Baseline: `92d5c825162eca3f35da647ef7f5cfd32275ed6f` on `main`.
Implementation and device qualification: 2026-10-06, ESP32-P4 COM13.

Implementation commit: `a15ee65` on `codex/m9-pattern-tools`. Qualification evidence
and this report are committed separately. The branch is local; no remote GitHub
run or merge is asserted.

Changed files: `src/app/model.h`, `src/app_main.cpp`,
`tests/pattern_tools_test.cpp`, `tests/analyze_m9.py`,
`.github/workflows/guition.yml`, `README.md`, `docs/VALIDATION.md`, this report,
and genuine stress/normal-boot serial captures.

## Architecture and ownership

The existing 12-byte `app::Command` and bounded SPSC queue are retained.
New kinds are Rotate, Reverse, CopyTrack, ClearTrack, Duplicate, Euclidean,
Randomize, Mutate and Reroll. Every command carries its pattern, track and
parameters. `step=1` requests all tracks for Rotate/Reverse; Euclidean uses it
for rotation. Randomize packs four bounded seven-bit parameters into `value`.
No generic messaging system, secondary queue or allocation was added.

End-of-run serial summaries are serialized between loop and UI and paced in
32-byte chunks with 4 ms intervals, outside audio, after captures showed lost
USB/JTAG packet fragments during burst reporting. No serial waiting or logging
was added to successful audio blocks. Qualification accepts only complete genuine
captures. These intervals occur after the measured audio capture has completed.

Audio applies at most sixteen commands at the start of an audio block, before
rendering samples. Each edit completes coherently in that boundary. UI uses the
existing optimistic mirror only after enqueue succeeds and the original command
acknowledgement protocol is unchanged. Rejected commands leave musical data
unchanged and restore confirmation state so the user can retry.

Tools change Pattern masks, metadata and, on duplication, copy length. Duplicate
also deliberately selects the destination for editing. They never copy Track
instrument/mixer state, sample pointers, mute/solo, phase, playing/queued pattern,
playback RNG or pending ratchets. Current accepted parents retain their captured
velocity/count; future onsets use edited data. Transport never restarts.
Editing nonplaying or queued patterns leaves current timing untouched.

Pattern remains 802 bytes; the bank remains 12,832 bytes per Engine. Rotation
uses a fixed 48-byte metadata temporary, reverse swaps in place, and mutate uses
a fixed sixteen-byte position array. Duplicate copies directly between slots;
there is no extra pattern bank, filesystem use, per-sample work or tool heap use.

## Musical semantics

* Rotate moves triggers and all metadata together by one position in the active
  length. Positive direction is right; negative is left. Hidden steps stay intact.
* Reverse maps `i` to `length-1-i` for triggers and metadata; hidden steps stay intact.
* ALL variants repeat the same operation for all sixteen tracks.
* Copy track copies its full sixteen-bit mask and all sixteen metadata entries,
  including hidden steps. Instrument, mixer, sample source, mute and solo remain.
* Clear track clears the entire lane mask and retains all metadata, like M8 CLEAR.
* Duplicate copies the selected pattern to the next slot, wrapping P16 to P01.
  Destination becomes the edit pattern without requesting a playback switch.
  Nondefault length, any trigger (including hidden triggers), or any nondefault
  metadata counts as occupied and requires explicit overwrite confirmation.

## Euclidean convention

For length N and pulses K, position i is active iff `(i*K)%N < K`.
This is the deterministic mechanical Euclidean word, anchored with a pulse at
zero when K is nonzero. Rotation moves right by R modulo N. Bit zero represents
step zero. Exact unrotated masks: E(3,8)=`0x49`, E(5,8)=`0xb5`,
E(5,12)=`0x529`, E(7,16)=`0x54a9`. K=0 clears the active region, K=N fills it.
Only active trigger bits change; every metadata entry and hidden bit is retained.
Steps derive from the selected pattern length. No engine-specific presets exist.

## Generation RNG and controls

`edit_seed` starts at `0x4d395031`, independent of M8 `seed`/`rng`.
Each generation operation initializes a local xorshift32 stream from that seed.
Range selection uses a 64-bit multiply; no hardware entropy or live probability
draws are used. Same seed, parameters and source yield the same exact result.
Reroll advances edit_seed by one xorshift32 draw; queue ordering keeps the audio
and optimistic UI mirrors aligned. PLAY does not reset the editing seed.

Randomize defaults: density 45%, velocity spread 20, probability spread 15 and
ratchet chance 5%. Controls all span 0–100. Active-region triggers use independent
density decisions; density zero clears and density 100 fills. Inactive-position
metadata stays intact. Generated active velocity is centered on 100 with uniform
integer spread ±VEL VAR, clamped to 1–126. Preexisting velocity 127 is retained
as an explicit accent. Probability is uniform in `[100-PROB VAR,100]`.
Ratchet chance selects 2–4 uniformly, otherwise 1. Hidden data is untouched.
Reroll changes the seed; APPLY then generates from the displayed seed.

Mutate defaults to 20%. Zero changes nothing. Other amounts choose exactly
`ceil(length*amount/100)` distinct active-region positions by partial
Fisher-Yates, toggle their trigger and vary velocity by at most ±5, preserving
127 accents and clamping other velocity to 1–126. Probability/ratchets remain.
This gives a strict edit budget rather than a flaky statistical promise. At short
lengths the minimum nonzero amount changes one position. Duplicate then mutate
provides a recoverable source workflow; no undo stack was introduced.

## TOOLS UI and dirty rendering

SEQ → TOOLS opens a dedicated page with TRACK, GENERATE and PATTERN tabs.
The persistent context shows pattern, track, length and editing seed. TRACK
has one-step left/right, reverse, copy and clear, plus track selection arrows.
COPY freezes the source, offers destination arrows and explicit CONFIRM/CANCEL.
CLEAR always requires CONFIRM/CANCEL. Duplicate asks before overwriting an
occupied slot. Pending context names the operation and frozen targets. Tab/page
navigation cancels pending actions. Destination selection has no pad hit targets.

GENERATE offers pulse/rotation controls and Euclid APPLY; density, velocity,
probability and ratchet macros with Random APPLY; separate mutation amount and
Mutate APPLY; and REROLL. Touch or drag macro/amount controls; APPLY is a distinct
button. Select a track in TRACK, then enter GENERATE. Pattern tabs provide
duplicate, rotate all left/right and reverse all. Targets are at least 52 pixels
high, with twelve 184×60 operation controls. There is no keyboard or progress UI.

Edits dirty context/status and at most sixteen sequence cells; those cells are
drawn on SEQ, while TOOLS redraws only its visible status/control rectangles.
Changing sections redraws their controls; entering a page uses the existing full
page pipeline. No transform sets full-screen dirty state or resets capture.

## Host and regression verification

`pattern_tools_test.cpp` covers lengths 1/3/7/12/16, odd/even reversal, exact
metadata mappings, rotation cycles/inverses, all-track transforms, hidden data,
full copy and source independence, clear metadata retention, exact Euclidean
masks and every rotation, fixed RNG/generation golden values, repeatability,
reroll difference, density endpoints, bounds, strict mutation counts including
zero, playback RNG/phase/queue preservation and pending-parent ratchet completion.
It also tests captured confirmation intent across navigation, metadata-only
overwrite protection, P16 wrapping, cancellation, touch target geometry and the
absence of pad hit targets. A global allocation counter covers 9,000 tool calls.

All prior PCM, eight-FX equivalence, display geometry/dirty history, app model,
pattern engine, groove engine and WAV/sample host tests are retained. Historical
M4/M4.1/M5/M6/M6.1/M7/M8 analyzers are retained unchanged. CI adds the M9 host test
and `analyze_m9.py`, which checks the new capture and invokes M8/M5 qualification.
Remote GitHub CI is not claimed by local validation.

Local builds use Python 3.11.7 and PlatformIO 6.1.19 from the installed PIO runtime;
CI retains its pinned PlatformIO 6.1.18 and the unchanged pioarduino platform.
The project `.venv` uses Python 3.13 and cannot load the installed Python 3.11
LittleFS extension, so it is used for host/capture scripts rather than firmware
builds. All ten firmware environments compiled successfully; all nine C++ host
tests and the existing analyzer/equivalence checks passed locally.

## Device qualification

Genuine capture: `GUITION_M9_STRESS_SERIAL.log`. The initial twelve seconds
combine the M8 16-track, 4× ratchet, 240 BPM, straight swing and enabled Delay
load with all-track rotations/reversals every 350 ms. Thereafter all tool kinds,
M8 groove, quantized switching, mute/solo, touch polling and display retirement
are exercised. TOOLS copy confirmation and randomize are also scripted through
the same UI handlers. `max` in `[M9 tools]` measures the complete audio block
including command application; `dense_blocks` proves edits under dense load.

The final complete capture passes `analyze_m9.py`, including its nested M8/M5
checks. Earlier incomplete USB/JTAG summaries were rejected and are not used as
acceptance evidence. Stress firmware SHA256:
`2683e3a461a67ab321de67330273974fb14dd7d91c4b07bdf0b7f7cf9a069ff6`.

| Measurement | Result |
| --- | --- |
| Audio blocks / sample duration | 10,337 / 60.006 seconds |
| Render + command p50 / p95 / p99 / max | 3,240 / 3,973 / 4,015 / 4,124 µs |
| Block budget | 5,804.989 µs |
| Worst headroom | 28.96% |
| Worst complete render block containing a transform | 4,124 µs |
| Blocks containing transforms / dense-load transform blocks | 151 / 121 |
| Deadline misses / write failures / timeouts / PCM rails | 0 / 0 / 0 / 0 |
| Active voices min / max | 16 / 16 |
| PCM peak / nonzero values | 7,599 / 5,290,992 |
| Initial dense triggers per second / worst block | 1,024 / 4,124 µs |
| Parents / passed / skipped / ratchet events | 14,976 / 13,768 / 1,208 / 39,053 |
| Pending maximum / velocity bounds / swing changes | 48 / 1–127 / 8 |
| Pattern switches / long→short / short→long / queue replacements | 8 / 2 / 2 / 4 |
| UI seconds / submitted / completed frames | 63.008 / 1,478 / 1,478 |
| Normal-frame dirty avg / max | 66,770 / 476,160 bytes |
| Full page frames / UI preparation max | 30 / 150,987 µs |
| TOOLS entries / copy workflows / apply-confirm taps | 4 / 4 / 8 |
| Maximum step cells dirtied / full draws caused by edits | 16 / 0 |
| Command rejects / touch errors | 0 / 0 |
| PSRAM free before / after | 30,885,508 / 30,885,508 bytes |
| Largest PSRAM block before / after | 30,408,692 / 30,408,692 bytes |
| Internal free heap before / after | 381,812 / 381,812 bytes |

Audio-applied operation totals: rotate 37, reverse 26, track copy 18, track clear
15, duplicate 14, Euclidean 14, randomize 18, mutate 15 and reroll 15. The M9 UI
row counts scripted control taps, while the M9 tools row counts actual
audio-applied commands. Normal-frame dirty traffic averages 8.69% of a full
frame. The high ordinary-frame maximum includes pattern-page changes and the
existing dirty-history/tile union; track tools do not dirty the full framebuffer.
No heap growth or largest-block loss occurred. Normal app RAM is 72,448 bytes;
flash is 636,216 bytes.

This page-heavy scripted run does not establish a strict 30 FPS guarantee.
Human touch feel, sound and visual assessment remain separate from the genuine
GT911 polling, rendering retirement, engine timing and host geometry checks.

Normal `guition_app` was restored to COM13 with flash hash verification. The
genuine `GUITION_M9_NORMAL_BOOT_SERIAL.log` confirms display/touch/audio startup
and synth fallback with the same expected no-card mount status. It is a boot
capture, not a second 60-second qualification. Normal firmware SHA256:
`3a487b2807a19c5ada7e68391f19f5e32e5f02a716c7e079cfb946bcb110a30e`.

Reproduce with:

```text
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M9_STRESS_SERIAL.log --seconds 90 --until "[M5 memory]"
python tests/analyze_m9.py docs/GUITION_M9_STRESS_SERIAL.log
pio run -e guition_app -t upload --upload-port COM13
```

## Scope and practical answer

M9 adds composition and variation workflows only. Parameter locks, microtiming,
persistence, undo history, song mode, MIDI, recording, slicing and new effects
remain outside the milestone. Normal startup does not generate patterns. Physical
SD/sample qualification remains pending and is not part of M9. Scripted UI tests
do not substitute for a human assessment of touch feel, picture or audible output.

P4SDM now supports fast interactive pattern construction and controlled variation
without parameter locks or persistence: Euclid/randomize create lanes, transforms
rearrange groove coherently, and duplicate/mutate preserves a source to return to.
Patterns and edits are still RAM-only and disappear when powered off.
