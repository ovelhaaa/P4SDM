# M14 — Sample Slicing + Waveform Editor

Accepted M13 `1119db4050e6a777c02e3431c1502583327343f2` was merged and
pushed to main as `837a818` with user authorization. M14 implementation:
`5833cb1`, on `codex/m14-sample-slicing`. Work stops after M14.

## State and editing contract

`Slice` stores uint16 start/end (4 bytes); `SliceBank` holds sixteen slots and
uint8 count/selected (66 bytes). Fixed, trivially copyable, allocation-free
storage uses absolute normalized sample coordinates 0..65535, with end>start
for valid edits. The bank and `slice_enabled` belong to Track, never Pattern.
Pattern copy/rotate/reverse/randomize/clear/duplicate and pattern changes leave
them untouched. StepLocks remains eight bytes with the same seven M12 locks.

`SliceBank::selected` is the Track's active audible default;
`Ui::inspected_slice[16]` is independent inspection. VIEW PREV/NEXT only
highlights; USE deliberately sets the active slice. SLICES OFF uses M13 Track
START/END; ON overrides those endpoints with the active region. Editing Track
START/END neither rewrites nor intersects existing slices. Those endpoints
remain the AUTO/RESET domain. Manual slices may overlap, contain gaps and be
out of chronological order; neighbors never move and slots are never sorted.

First successful assignment preserves M13's full-region, forward, OneShot,
choke-off, C4 defaults and creates one full slice. Replacement retains
normalized slices, active selection, enabled state, pitch and other settings.
Future triggers remap against the new frame count; source switching retains
configured sample state. RESET SLICES restores one current-Track region and
active=0, retaining enable, assignment, pitch, reverse, mode, choke, tone/send.
An invalid reset domain is repaired to one normalized unit.

AUTO supports exactly 2/4/8/16. The button displays/applies the next count.
Integer boundaries are `start + uint32_t(end-start)*i/N`: exact first/final
endpoints, no gaps/overlap and no zero-width regions. Insufficient/inverted
domains reject without mutation and display a message. ADD splits inspected
[A,B) at M=A+(B-A)/2 and inserts [M,B) after [A,M); it rejects count=16 or
width<2. DELETE rejects count=1 and shifts higher slots down. Both preserve
surviving active slot identity when shifted and clamp deleted-last selections.
The inspected index is clamped after structural edits.

## Playback and future M15

`resolve_event(..., optional_slice_index)` selects endpoints at acceptance.
An explicit index supports audition and the future M15 resolution boundary.
The resulting descriptor is the existing eight-byte M13 Playback. Its source
is invisible to the renderer. TriggerEvent uses its formerly reserved byte for
zero=unsliced or index+1 telemetry, retaining its 20-byte size and no pointers.
Accepted pending events are copied by the unchanged scheduler; ratchets keep
the captured region through selection, edits, divide, delete or reset. Active
voices never jump; the next parent sees current Track state.

Every slice uses the unchanged `trigger_voice`/`resolve_region`/Voice path.
Voice layout and Transfer ownership are unchanged, as are normalized PCM
resolution, reverse, pitch 48/60/72=0.5x/1x/2x, Gate/OneShot, choke, fades,
velocity, M12 locks, filter, pan and effective Delay send. No slice lookup is
added to Voice::next and no analysis/allocation/filesystem call occurs in audio.
Rejected parents still neither trigger nor choke. Tiny regions can be silent
with the accepted M13 production fades while remaining bounds-safe.

AUDITION snapshots the inspected region at velocity 127 using Track pitch,
reverse, mode, choke and normal tone/send, even when SLICES is OFF. It does not
change active selection. Gate release on lift retains the audition's Track
across selection/page changes; a full release queue retries before another
audition. The fast normal Sequence pad path is unchanged. Page-local
pad-per-slice audition is deliberately deferred.

**Yes:** M14 is a stable slice abstraction on M13 such that M15 can supply an
optional per-step slice index before the same event-resolution boundary,
without changing PCM ownership, the sample renderer or sequencer scheduler.
No Slice Lock is implemented in M14.

## Waveform ownership and drawing

Each resident Sample embeds 360 signed int16 min/max pairs (1440 bytes), two
screen pixels per bin over the visible 720-pixel width. Frame partitions use
64-bit integer math. Short samples repeat the source frame for empty integer
partitions; null/zero input produces initialized zeros. Generation is O(frames)
at load only and never allocates a large PCM copy.

Storage builds the complete cache after WAV decode, before Transfer::publish,
yielding one RTOS tick per column. Build latency includes those yields. Fixtures
are analyzed in initialization before audio begins. A temporary 4 MiB,
2097152-frame PSRAM fixture exercises the responsive build and is freed before
the stress baseline. No waveform analysis runs during UI redraw or audio.

PCM, frame count and waveform transfer as one immutable Sample. After audio
acknowledgement, storage swaps the UI preview pointer under a short critical
section before freeing retired PCM/metadata. UI copies name/frame count/cache
together under that protection into one fixed UI-owned Preview. UI never
retains a Sample pointer or sees partial/freed analysis. The waveform page
uses that same coherent name and uppercases it for the existing display font.
Revision is captured before copying so concurrent publication causes another
redraw. Qualification uses the same preview helper before tasks start.

SAMPLE PLAYBACK adds SLICE EDITOR. The 800x480 page has a 720x116 waveform,
176x48 inspection/active/mode buttons, 368x56 START/END sliders, 176x56 AUTO/
ADD/DELETE/RESET buttons and 176x56 audition/Track navigation/BACK. Bottom
controls replace global navigation on this page; BACK restores it. PREV/NEXT
words work with the font, which lacks arrow glyphs. Yellow boundaries/fill
identify inspection; a cyan underline identifies active playback.

The existing queued native framebuffer is the display cache. Preview copying
occurs on Track/publication changes. Drawing visits only 360 cached bins using
vertical rectangles, never PCM. Unchanged waveform pixels remain cached;
boundary drags dirty the waveform and corresponding value, not the whole
screen. Structural edits dirty bounded widgets; page/Track changes redraw fully.
First-page/partial preparation includes framebuffer repair and queued display
fencing, distinct from pure waveform drawing and audio block costs. Repeated
full-page stress changes do not guarantee 30 FPS. Scripted coverage is not a
claim of human visual/touch or audible verification.

## Tests and qualification workload

Sixteen C++ suites pass, including all thirteen prior suites. New tests cover
exact AUTO boundaries on full/trimmed domains, short-domain rejection, default/
reset/add/delete/max count, active identity/clamping, gaps/overlaps/unordered
indices, Track ownership, and touchscreen geometry/hit routing. Known PCM
checks exercise multiple slices forward/reverse at pitches 48/60/72 and both
modes, corrupted bounds, parent/ratchet snapshots after edits/delete/recreation,
probability rejection and safe replacement against a new frame count/waveform.
100000 control/voice iterations assert no new allocation. Exact waveform
min/max tests cover constant, impulse, alternating and ramp fixtures at
1/7/360/720/721/2097152 frames and null/zero input, with yield-count assertions.
The retained M13 suite covers fades/choke/velocity/actual filter/Delay, and the
Transfer suite still poisons/frees retired PCM across 10000 publications.
Retained scheduler/DSP/FX equivalence and historical capture checks pass.

Qualification retains sixteen distinct 65536-frame PSRAM buffers and the same
2097152-byte PCM pool. Banks initially contain 2/4/8/16 slots with varied
indices, reverse and Gate. Dense active slices are intentionally broad,
independent overlapping regions to keep all sixteen voices running even under
the highest retained pitch locks; other slots retain equal divisions. First
12 seconds keep choke OFF, 240 BPM, four ratchets, filters, M12 locks, Delay,
display and 16/16 voices. Later sections add slice edits/selection/AUTO/add/
delete/reset/audition/page changes while retaining M13 choke/Gate coverage.
The qualification script explicitly completes inherited lock/tone/pattern
workflows before competing slice navigation can interrupt them. No analyzer
assertion, deadline (5804.989 us), workload or >=20% threshold is relaxed.

Final genuine SAMPLE run: 10337 blocks / 60.006 seconds, p50/p95/p99/max
3036/3278/3352/3566 us, 38.57% worst headroom, zero deadline misses, I2S
failures/timeouts, rails, UI rejection or heap loss. Dense workload: 2068
blocks, 16/16 active voices, 240 BPM, four ratchets, 1024 hits/second.
Across the run: 52558 slice triggers, indices 0..15, lengths 1..65536 frames,
22 auditions, 46 active-selection commands, 92 divisions, 9 adds, 23 deletes,
22 resets and 233 command/edit blocks (max 3486 us). Edit counters count applied
commands; UI prevents invalid ADD/AUTO/DELETE requests before sending.

UI: 81 slice-page entries, 248 edits, zero edit-induced full redraws;
first-page preparation 111670 us, page preparation max 117479 us, pure waveform
redraw max 17543 us, partial slice-page preparation max 68003 us, drag dirty
max 280576 bytes. Partial timings can include queued fence waits and concurrent
inherited widgets. Total stress UI: 1323 submitted/completed presentations,
162 full redraws and 216 skipped frames.
Waveform: 17 builds before stress (16 resident plus temporary large fixture),
max/4 MiB wall latency 360228 us, including 360 cooperative yields. Generation
performs no runtime audio work. No >30-FPS guarantee is inferred from this.

### Fixed memory

| Structure | M13 bytes | M14 bytes |
| --- | ---: | ---: |
| Slice / SliceBank | — | 4 / 66 |
| Track | 48 | 116 |
| Engine / two Engines | 46880 / 93760 | 47968 / 95936 |
| StepLocks / TriggerEvent / VoiceState | 8 / 20 / 24 | 8 / 20 / 24 |
| SampleVoice / sixteen voices | 64 / 1024 | 64 / 1024 |
| Pattern / bank | 2850 / 45600 | 2850 / 45600 |
| Resident Sample metadata | 116 | 1556 |
| Waveform per resident / UI Preview | — | 1440 / 1540 |

Track adds 68 bytes including alignment, Engine 1088 and two Engines 2176.
Sixteen fixture descriptors use 24896 bytes, including 23040 bytes of immutable
waveform caches; fixture PCM remains 2097152 bytes. One fixed UI preview adds
1540 bytes and sixteen preview pointers add 64; counters/dirty arrays add small
fixed storage. No bank or per-trigger allocation occurs. The 4 MiB temporary
fixture is freed before before/after heap measurements. Normal firmware has
no resident fixture descriptors, PCM or analysis allocations.

SAMPLE internal heap before/after is 285492/285492; PSRAM free is
28722816/28722816, largest 28311540/28311540. SYNTH internal heap before/after is 310404/310404; PSRAM free
30885508/30885508 and largest 30408692/30408692. Its audio
p50/p95/p99/max is 3372/4042/4133/4380 us (24.55% worst headroom), with
16 active synth voices, zero errors and zero sample/slice/waveform residue.

Normal linked static RAM is 75360 bytes (23.0% of the board's reported
327680-byte budget); Flash is 656264 bytes. Runtime free internal/PSRAM
measurements include task stacks, display and Delay allocations and are
reported separately. Display uses the retained 2304000-byte PSRAM buffers
and Delay the retained 352800-byte storage. No unexplained per-trigger growth
or qualification allocation is present in normal firmware.

## Limitations and physical status

**PHYSICAL M14 SAMPLE/WAVEFORM VALIDATION PENDING**

COM13 reports NO CARD / MOUNT ERROR. The 4 MiB analysis case uses decoded
PSRAM PCM, not a physical WAV read. Real WAV loading/waveform, audible auto/
manual/reverse/Gate/choke audition and human touch/visual QA remain pending.
No transient detection, Slice/sample-selection/start/end locks, time-stretch,
BPM matching, granular playback, interpolation upgrade, persistence, MIDI or
song mode is included. Only AUDITION is provided; performance pads await M15.

`GUITION_M14_SAMPLE_INITIAL_SERIAL.log` is deliberately rejected evidence:
audio/heaps passed, but competing scripts lost inherited pattern-length and
lock/tone UI coverage. The fix completes those workflows without reducing
checks or the dense workload. Later complete captures pass inherited criteria.
The first host upload hit CP1252 output failure; UTF-8 output in the existing
Python 3.11 PlatformIO environment resolved it. No firmware redesign masks it.

## Changed files

- `src/app/slices.h`, `src/app/waveform.h`: fixed bank and load-side analysis.
- `src/app/model.h`: Track ownership, commands, accepted region, inspection and geometry.
- `src/app/wav.h`: resident metadata only; Voice/Transfer code unchanged.
- `src/app/samples.h`, `src/app/samples.cpp`: coherent safe preview and analysis.
- `src/app_main.cpp`: UI, audition/release, telemetry and qualification.
- `platformio.ini`: twelfth qualification environment, all others retained.
- `tests/slice_bank_test.cpp`, `tests/waveform_test.cpp`, `tests/slice_playback_test.cpp`: host checks.
- `tests/analyze_m13.py`: explicit inherited sizes with historical defaults intact.
- `tests/analyze_m14.py`: strict inherited plus slice/cache/normal-residue checks.
- `.github/workflows/guition.yml`: all retained checks plus M14 builds/tests/captures.
- `README.md`, this report and genuine serial logs: workflow and evidence.

## Reproduction and CI

Use the existing `C:\.platformio\penv\Scripts\python.exe -m platformio`
(Python 3.11.7, PlatformIO 6.1.18), with PYTHONUTF8=1 and
PYTHONIOENCODING=utf-8 on this Windows host. Let verified uploads settle
at least eight seconds before capture issues its separate reset.

```text
pio run -e guition_sample_slice_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M14_SAMPLE_STRESS_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m14.py docs/GUITION_M14_SAMPLE_STRESS_SERIAL.log
pio run -e guition_app_stress -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M14_SYNTH_STRESS_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m14.py docs/GUITION_M14_SYNTH_STRESS_SERIAL.log --synth
pio run -e guition_app -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M14_NORMAL_SERIAL.log --seconds 100 --until "[M5 memory]"
python tests/analyze_m14.py docs/GUITION_M14_NORMAL_SERIAL.log --normal
```

All twelve pinned firmware environments build successfully. All sixteen host
suites, six retained equivalence/FX scripts, and every historical capture
workflow step pass locally. M13's merged main also passed hosted CI:
[run 37561453539](https://github.com/ovelhaaa/P4SDM/actions/runs/37561453539).
M14's workflow retains all previous steps and adds its build, three host suites
and SAMPLE/SYNTH/normal analyzers. Hosted M14 results are tracked in
[branch CI](https://github.com/ovelhaaa/P4SDM/actions?query=branch%3Acodex%2Fm14-sample-slicing)
and the associated PR Checks; the final handoff reports the completed result.

Firmware SHA256 (verified uploads):

- `guition_sample_slice_stress`: `9217ba3d4041b0eed6e8358b8377d017ad4e4702c74c8d03b2f94e485b4cd978`
- `guition_app_stress`: `ce7f322d14118f0389b510b6a0bb9911c60ea9fd8bb9d1daa1d9cd3170bd06a4`
- `guition_app`: `83ddc7a316c3871fcdfbb63454369896861a9108173240369fa36e7dc62b5cfb`

## Restored normal firmware and final local result

Normal `guition_app` is restored to COM13 with upload hash verification.
Its genuine 60.006-second run has 10337 blocks, p50/p95/p99/max
1748/1767/1774/1878 us (67.65% worst headroom), zero misses/failures/timeouts/
rails, zero nonzero PCM/peak, zero sample voices, slice triggers, waveform
builds or slice edits. UI submits/completes one initial frame. Internal free
heap remains 310452/310452; PSRAM free remains 30885508/30885508 and largest
30408692/30408692. Normal internal growth versus accepted M13 is exactly
4096 fixed bytes; normal PSRAM free/largest are unchanged. Sample's additional
resident waveform descriptors account for 23040 more bytes, with 16 additional
qualification-script bytes. No heap loss occurs in any of the three runs.

All three final captures pass strict M14 plus inherited analyzers. In-memory
DSP/realtime/normal qualification passes; physical SD/audible/human UI validation
remains pending as stated above. M14 introduces no out-of-scope capabilities.
