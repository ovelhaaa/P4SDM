# M7 — Pattern engine and performance sequencer

Continues accepted M6.1 from `ef941a738a936ea561fd679f69d1a21295b32a9b` on `codex/m7-pattern-engine`. Stops at M7: no persistence, song mode, MIDI, probability, ratchets, accent or automation locks. Physical SD/sample qualification remains intentionally pending and does not block this milestone.

## Model and ownership

`Pattern` is trivially copyable: 16 uint16 track step masks and uint8 length (34 bytes including alignment; 544 bytes for all 16 slots). Slots initialize empty with length 16, matching the preceding app's empty sequence. Track synth/sample parameters remain independent of musical pattern data. Mute is track performance state and solo is a uint16 performance mask, outside Pattern. Runtime selection, queue, clock, counters, UI modes, touch capture and sample pointers are excluded from Pattern.

The audio task alone mutates and sequences its Engine. UI maintains a separate optimistic Engine mirror. Existing 64-entry SPSC audio command queue, 128-entry touch event queue and 32-entry low-latency pad queue remain fixed-size; rejection never overwrites existing commands. Audio processes at most 16 ordinary commands and 16 pad commands per 256-frame block. A pattern identifier uses previously available command padding: `Command` remains 12 bytes. Step command intent explicitly includes pattern, track and step. Copy carries source and destination IDs and copies only 34 bytes inside audio at the block boundary. Length/copy/clear/selection/mute/solo are bounded commands, without locks or allocation in render.

Audio publishes playing/queued IDs and a 16-bit processed-command acknowledgement in one release/acquire uint32 atomic. UI accepts that transport snapshot only when it acknowledges all UI commands sent so far, protecting pending optimistic edits from older audio publications. Audio playhead/trigger flashes remain atomics. UI never uses its own simulated clock to switch playing patterns. Audio stays highest priority/core0; exact 44.1kHz rational sixteenth timing, 256-frame audio, sample Transfer retirement, source-aware pitch, Delay, NativeQueued display and touch-worker pad audition are retained.

## Musical semantics

- Playing, selected/edit and queued slot are distinct. Default PATTERN taps select/edit and queue. INSPECT taps change edit selection without changing the playing or queued slot.
- While playing, the current pattern completes its configured loop. On its next exact step onset, audio switches to the queued slot and triggers that slot's step 0 once. Latest queue wins. Tapping the playing slot in QUEUE mode cancels a pending switch. No display-FPS timing dependency.
- While stopped, default selection activates immediately. INSPECT remains edit-only. PLAY always starts selected/edit slot at step 0 with phase reset. STOP cancels the queued switch, resets transport and adopts the selected/edit slot; existing voice/Delay tails continue.
- Length is clamped 1–16 at a block boundary. A live head is not moved by the command. At the next musical onset, if head+1 is beyond the updated length, wrap once to zero (and honor the queue); otherwise advance once. Increasing length extends the current loop. Hidden steps beyond length retain their data for later extension.
- COPY snapshots all 16 masks and length from source to destination, without copying mute/solo, track settings, selection, queue or UI state. Subsequent edits are independent. Copy into playing is supported at the command block boundary; phase/head remain unchanged until the next onset.
- CLEAR zeros only masks, retains length and leaves other patterns/performance state unchanged. Playing-slot clear becomes visible at the command block boundary, suppressing subsequent sequence triggers; it does not cut already sounding tails.
- Muted tracks receive no new sequencer triggers. If any solo bits are set, only soloed and non-muted tracks sequence. Multiple solos are supported. Manual pads bypass both mute and solo for synth and sample audition.

The [original-project audit](GUITION_M7_TAB5_AUDIT.md) documents preserved semantics and deferred melodic/first-step/song/file behavior.

## 800×480 interaction and rendering

Tap the large pattern field in the header to open PATTERN. A4×4 grid has 124×68 cells. PLAY/NEXT text distinguishes playing/queued state; selected cells have accent edges plus EDIT text. The header shows real playing, queued and edit slot IDs. Length +/- targets are 64×64, alongside the length value. COPY/CLEAR/QUEUE-or-INSPECT targets are 216×64.

COPY captures selected source, displays its number and asks for a destination cell. Destination selection overwrites without changing playback/edit selection. Tap CANCEL COPY to exit. CLEAR changes to CONFIRM CLEAR; a separate second touch confirms. Selecting a slot, changing length/mode or navigating cancels confirmation; navigating cancels copy. A queue rejection restores the prior workflow so the user can retry.

TRACK exposes selected track and source, MUTE and SOLO. Pads label MUTE/SOLO alongside source where applicable, preserve selected color, and flash on triggers. SAMPLE keeps LENGTH/WAVE visibly SYNTH ONLY and rejects their touches/drags; its pitch control retains M6.1 unity/tuning semantics.

Pattern transport updates dirty previous/new playing and queued cells plus the header field. Edit selection dirties affected cells and length value. Length-only edits dirty the value only. Copy/clear dirty their destination and workflow controls. Header pattern/track/BPM fields redraw separately. Normal full-screen redraws remain initial/page transitions, independent of sequencer onsets.

## Tests and qualification

`tests/pattern_engine_test.cpp` adds exact sample onset/order checks at 97/123/400 BPM and lengths 1/3/7/12/16; full 16→7→16 transitions; queued P3; latest P2→P4 replacement; inspect with a different pending slot; explicit pattern/track/step intent through a queue; pending-switch STOP and PLAY-after-inspect; queue cancellation; live shortening/extension and clamping; copy masks/length/independence/self-copy/playing destination; clear including playing destination; unchanged performance state; invalid inputs; no-mute, one-mute, one/multiple-solo, muted+solo, all-muted and manual audition. UI tests cover all 16 cells, length +/- bounds, copy/cancel/destination without selection, clear confirmation/cancellation/navigation, inspect, solo, target sizes and pairwise nonoverlap, plus bounded dirty regions.

All seven C++17 host binaries pass with Wall/Wextra/Werror: PCM mono, FX safety, display geometry, dirty history, app model, pattern engine and WAV/sampler. Prior FX output comparison, M4/M4.1/M5/M6/M6.1 recorded-log analyzers pass. CI retains all ten firmware environments and prior checks, adds the pattern host test and `analyze_m7.py` for genuine evidence. No card dependency is added.

Final device measurements are recorded below. Scripted interaction does not establish human touch ergonomics, picture integrity or audible quality. These hands-on checks remain separate; physical SD/sample behavior remains the existing M6 qualification limitation.

## Changed files

- `src/app/model.h`: pattern state, selection/timing/performance logic, compact commands, pure UI workflows and hit geometry.
- `src/app_main.cpp`: safe transport publication, PATTERN page/header, source-aware TRACK solo, bounded dirty updates, continuous scripted stress and audio-applied telemetry.
- `tests/app_model_test.cpp`: adapt previous sequence tests to pattern-owned masks.
- `tests/pattern_engine_test.cpp`, `tests/analyze_m7.py`: new pure-state/UI/boundary tests and real-device qualification analyzer.
- `.github/workflows/guition.yml`: preserve the build matrix and prior checks; add M7 checks.
- `README.md`, this report, original audit and genuine stress/normal-boot serial logs: user instructions and qualification evidence.


## Genuine device stress — 2026-10-06, COM13

Final capture: `GUITION_M7_STRESS_SERIAL.log`, validated by `tests/analyze_m7.py`.
The stress-only startup seeds P1 (length 16) and P2 (length 7) with all tracks/all
steps, starts continuous playback at 240 BPM, keeps Delay active and exposes 16
synth voices. A repeating 12-second UI script queues P3 then P2 before the loop
boundary, returns to P1, changes/restores length after the transition, copies to
nonplaying P4, inspects/clears P4, edits steps, toggles mute/solo, auditions pads,
drags source controls and navigates Sequence/Track/FX/Pattern. Touch polling and
NativeQueued display remain active. No filesystem action is introduced by the
script; the unchanged M6 loader attempts one mount and reports no card.

| Measurement | Final result |
|---|---:|
| Captured audio | 10,337 × 256 frames = 60.005 s |
| UI stress interval | 63.008 s |
| Render p50 / p95 / p99 / max | 3,062 / 3,797 / 3,839 / 3,908 µs |
| Block deadline | 5,805 µs |
| Deadline misses / write failures / timeouts | 0 / 0 / 0 |
| Active voices min / max | 16 / 16 |
| PCM peak / nonzero values / rails | 7,597 / 5,291,160 / 0 |
| Audio-observed switches / loop wraps | 10 / 79 |
| Actual 16→7 / 7→16 switches | 5 / 5 |
| Audio-applied queue replacements | 5 |
| Audio-applied copy / clear / length edits | 5 / 5 / 20 |
| Audio-applied solo / mute / step edits | 10 / 10 / 10 |
| UI submitted / completed frames | 1,091 / 1,091 |
| Full frames (initial/page transitions) | 26 |
| Normal frames | 1,065 |
| Normal dirty bytes avg / max | 30,375 / 440,320 |
| All-frame dirty bytes avg / max | 47,953 / 768,000 |
| Touch errors / rejected commands | 0 / 0 |
| UI preparation max / skipped slots | 123,515 µs / 26 |
| PSRAM free before / after | 30,885,508 / 30,885,508 bytes |
| Largest free PSRAM block before / after | 30,408,692 / 30,408,692 bytes |
| Internal free heap before / after | 406,964 / 406,964 bytes |

No heap growth, reboot, clipped PCM or audio deadline failure occurred. Normal
updates never submitted a full framebuffer. Dirty telemetry includes the existing
NativeQueued alternating-buffer history/coalescing; maximum preparation includes
page reconstruction. This is not a guarantee of 30 FPS or human touch/visual QA.

## Readiness and limits

**Is the P4SDM sequencer now suitable for live multi-pattern use even before
project persistence and SD sample qualification are complete?** **Yes, for the
implemented in-memory pattern and synth performance workflow:** deterministic
state tests and the physical continuous multi-pattern stress pass. Reboot loses
patterns by design. This does not qualify card loading, real sample performance,
audible output, or hands-on screen/touch ergonomics. Those remain explicit
separate checks; physical SD qualification remains pending without blocking M7.

No subsequent milestone was started. Remote GitHub Actions is not triggered by
local commits; local CI-equivalent results are reported separately below.

## Final verification

On 2026-10-06 all ten firmware environments pass on the pinned pioarduino
55.03.36-1 / Arduino 3.3.6 toolchain: boot, audio, synth, FX, display, M4 UI/audio,
M4.1 pipeline, app, app stress and sampler qualification. Local build matrix
elapsed time: 5m04s. Seven host binaries, previous FX comparison/log analyzers and
the new M7 device analyzer pass. The workflow retains every preceding build/check
and adds the pure pattern test plus genuine M7 capture validation. Remote Actions
was not run or claimed green in this session.

Platform setup attempted to refresh its Python dependencies despite the pinned
installed toolchain. Local verification used a temporary ignored `.pio` launcher
that makes dependency reachability checks offline, preserving all installed
packages and the repository's pins. No workaround or toolchain change was added
to CI or committed to the repository.

Normal `guition_app` was successfully restored and hash-verified on COM13 after
stress qualification. `GUITION_M7_NORMAL_BOOT_SERIAL.log` confirms codec/I2S/PA
startup and the expected single no-card mount result. The board is left running
normal firmware, with empty RAM patterns and no automatic stress sequence.
