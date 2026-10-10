# M22 — touchscreen groovebox refinement

**TECHNICAL PASS / PHYSICAL UX PASS — user walkthrough reported successful.**
Final remote Actions acceptance is tracked in PR #17. Automated and device
qualification results below are independent of human finger/listening acceptance.
No M23 work, DSP changes, migration, or persistent UI fields are included.

## 1. Baseline and reproducibility

Source/main: `72a9178957a2cf048e05cf86ce403509756284df`, verified against fetched
`origin/main`. The existing untracked `docs/m21/error.png` was preserved.
Before firmware edits, `guition_app` and `guition_app_nearest_legacy` built,
the inherited host suite passed **108/108**, and the real production drawing
functions rendered all views. Baseline builds and source/package/hash manifests
are archived under `.pio/m22-artifacts/baseline/`.

Use `C:/.platformio/penv/Scripts/python.exe -m platformio` on this workstation:
the default Python 3.14 is unsupported by the pinned platform; the repository
3.13 environment also picks up 3.11 binary extensions from PlatformIO's penv.
The existing PlatformIO 3.11.7 environment builds successfully. This is a host
toolchain issue, not a firmware change.

Baseline serial startup verifies ES8311, 44,100/16 I2S, SD 4-bit/20 MHz and
nine indexed entries. It does not establish finger interaction or listening.
Inherited M21.4 device logs remain historical timing evidence; final M22
captures must pass the original strict analyzer independently.

## 2. Interface audit

Evidence labels: **S** = source-derived; **R** = inspected host framebuffer;
**H** = physical human observation. No H observation is inferred from S/R.

| Section / subtabs | Findings and retained workflow | Evidence |
|---|---|---|
| SEQ | Pads select and audition; steps toggle directly and select for EDIT STEP. Bank A/B retains selected Track/Step. White step edges identify inspection; originally the yellow playhead replaced the entire ON/OFF fill. Track activity similarly replaced selection fill. Separate activity stripes now preserve canonical fill. Tempo +/- remains in the header. | S,R |
| TRACK Main / Tone | Track/source context persists in header. Main has volume, pan, pitch, length, wave, mute/solo, source and Sample entry; Tone has cutoff/resonance/send/Delay. SYNTH-only wave/length are guarded. Source changes force complete context refresh. Sliders own a drag even beyond their rectangle and clamp at endpoints. | S,R |
| SAMPLE Browser / Playback / Slice / Auto / Slice Tools | Browser requests asynchronous WAV load, with status from storage completion. Browser filename was scale 1; now scale 2 and still bounded. Playback exposes region/reverse/mode/choke. Slice VIEW remains separate from USE ACT; proposal markers stay magenta/dashed, accepted/inspected boundaries cyan/yellow. Auto Analyze does not apply; Apply waits for accepted acknowledgement. Destructive slice tools require captured-intent confirmation. | S,R |
| PATTERN Select / Step / Basic,Tone,Sample Locks / Tools | Edit, playing and queued Pattern are independent. Inspect does not queue. Step selects metadata for the captured Pattern/Track; lock commands name Pattern/Track/Step explicitly. Unsupported SYNTH/SAMPLE locks are guarded without erasing stored data. Navigation retains selection and cancels hidden destructive intents. | S,R |
| PERFORM Patterns / Mix / Repeat / Chain | E/A/O/F and pending markers distinguish temporary launch from arrangement/editor state; base/temporary mix flags remain separate. Repeat retains accepted-event capture, quantization and Stop retry. Its owner survives navigation until release. Chain rows and controls remain 52 px high; Delete captures intent and Clear confirms. Full redraw scripts exercise page traversal. | S,R |
| PROJECT Home / Naming / Confirm / Load | Captured operation/name survives selection changes; Cancel and navigation clear intent. `request()` can reject busy/unavailable storage; `busy()` represents pending work. Only successful verified storage save reports PROJECT SAVED (`projects.cpp`). Load validates before apply, stops transport and restores samples incrementally; missing names remain SAMPLE/silent. CRC/recovery codec tests are inherited. | S,R |

Two additional S concerns were corrected: sample/project request rejection was
silent, and Project request publication increments revision before the storage
status necessarily changes. UI reports REQUESTED, PENDING and REJECTED, watches
busy transitions independently of revision, and disables pending Project actions.
A previous SAVED notification cannot replace pending status in header or Project
row. Completion/error still comes from storage; persistence is unchanged.

Source geometry tests cover all nineteen pages, footer/header, edge pixels,
nonoverlap and minimum **52×52** actionable rectangles. Segments use the same
registry parent and shared subdivision as drawing/hit testing; no thin slider
handle is an independent target. Existing slider arithmetic clamps drag beyond
edges. Disabled controls remain visibly muted or labeled unsupported and reject
unsupported actions. No new long-press shortcuts are introduced.

Common paths: Track pad → step → EDIT STEP → velocity/probability → SEQ;
Track pad → SAMPLE → previous/next → LOAD → Playback/Slice/Auto; PATTERN →
Step/Locks; PERFORM → Patterns/Mix/Repeat/Chain; PROJECT → Save/Save As/Load/New.
All footer destinations take one tap. Exact repeated file-browser tap counts
depend on index position. Physical legibility, missed touches and relative
responsiveness received the user's overall walkthrough pass; no more detailed
per-control subjective ratings were supplied.

## 3. Visual evidence

These images are **simulated output from the actual production framebuffer
renderer**, not LCD photographs or handcrafted mockups. Fixtures include Track
16, Pattern 16, BPM 400, maximal locks, 16 slices, Chain x16, long filenames,
missing sample/error state, SYNTH-disabled playback, Project Confirm and Load.

![Before/after production framebuffer renders](m22/before_after.png)

[All sections/subtabs](m22/sections.png). Reproduce with
`python tests/m22_screens.py`; the before source is the exact M21.4 merge, not
the older M19 `render_ui.py --baseline` mode. Correct enum-to-label mapping is
derived from the production Page enum. Rendering asserts zero allocations.

## 4. Changes by section and renderer

SEQ preserves ON/OFF and selected Track fills during playhead/sounding activity
using an independent yellow right-edge stripe. The selected Step keeps white
top/bottom borders and locks keep their small marker. All sections get scale-2
HEAR/queued/Repeat status. SAMPLE browser filenames use scale 2. All text remains
bounded, with clipping and truncation; no new font package is used.

Full-page rendering skips header controls already drawn by `header()`.
The 3×5 font merges identical adjacent rows and contiguous lit bits into
rectangles, with exactly unchanged glyph pixels. Exhaustive production-font
comparison at scales 1/2/3/4/8, clipped positions and ASCII 32–126 passes:
**41,400 → 19,080 rectangle calls** (53.9% fewer in that fixture). This is a
draw-call result, not a claimed 53.9% device speedup. Existing waveform bins,
framebuffers, dirty tiles, repair histories and display fence are retained.

## 5. Workflows A–E

| Workflow | Automated status | Human status |
|---|---|---|
| A: beat, Track/bank/step/tempo/velocity/probability, return | Inherited navigation, geometry, model, groove and rendering checks | User overall pass |
| B: WAV/region/slice/audition/transients Apply/Cancel | Inherited parser/ownership/waveform/slice/transient-service tests; real SD qualification reported separately | User overall pass |
| C: expressive Step/locks and editor/playback context | Explicit command context, unsupported lock/source tests, immutable event replay | User overall pass |
| D: Override/Fill/Mix/Repeat/Chain | Inherited scheduling/equivalence; added gesture/release overflow checks | User overall pass |
| E: Save/Save As/Load/New/cancel/recovery/missing | Inherited codec, captured modal intent and recovery tests; final real SD evidence reported separately | User overall pass |

The A–E pass is the user's response **“everything seems ok”** to the explicit
workflow/missed-touch/legibility/artifact/freeze/Repeat/audio question on
2026-10-10. It is an overall qualitative report, not five detailed recordings.

## 6. Touch path and ownership

GT911 polls at an 8 ms task delay plus I2C work (10 ms individual transaction
timeout). Native bounds/orientation are validated before logical hit testing.
The touch task publishes bounded SPSC UI packets and independently queues pad
audio, so accepted audio and displayed UI are distinct boundaries.

UI packets now carry the observation timestamp, initial-press flag and release
generation. A dropped release cannot leave UI `down` latched. Reconciling the
independent generation cancels the old capture and releases its slice audition
and Repeat owners; a saturated command queue retains the existing release
retry. A move whose press was dropped cannot become an unrelated tap. Release
never hit-tests another widget. Navigation clears capture but preserves the
original release owner. Source/Track/sample/Project context replacement clears
capture and releases held owners, preventing a drag from silently retargeting.
Three consecutive failed GT911 polls cancel a held gesture through the same
release generation. Recovery suppresses the stale held state until a real
release is observed; it cannot replay the previous press. Deterministic tests
cover transient-error recovery, one cancellation and lift/repress gating.

No audio command kind, queue acceptance rule or musical Repeat window changed.
The GT911's last state intentionally persists between nonfresh packets; if the
controller never supplies a release packet, software cannot distinguish a
legitimate long hold from controller failure. No guessed hold timeout was
introduced. Actual controller-failure behavior remains a physical acceptance
item. Independent generation covers a missed UI queue event, not an unobserved
physical lift that never reached the controller reader.

## 7. Bounded response instrumentation

Eight fixed 256-bin histograms (1 ms bins, separate overflow/max) measure:
poll-call duration, observation-to-dispatch, dispatch/state-update, command
enqueue-to-observed audio acknowledgement, redraw preparation, present-call
duration, full-page preparation and controller-observation-to-display-submission.
They allocate no memory and do not instrument pixels/audio callbacks.
Quantiles are explicitly labeled **upper bounds**; overflow uses measured max.
The acknowledgement boundary is observed by the UI from the existing audio
publication; it includes any UI sampling delay. Poll-call duration cannot
measure the interval from finger contact to the next poll.
The normal application's read-only serial `U` query prints the histograms;
`tests/m22_human_capture.py` requests them every 15 seconds during a walkthrough,
without injecting coordinates or musical commands. Background empty taps and
unowned moves do not start a submission-latency sample. Samples represent
the oldest eligible pending packet in a coalesced redraw, not every individual
packet. Successful controller recovery still requires a fresh observed lift.

Display `present()` telemetry includes repair/cache/transfer/submission work
as provided by the existing HAL; it is not a scanout or visible-pixel timestamp.
Existing completion fences are preserved. Controller event quantiles with zero
events are **not measured**. Redraws under stress mix scripted navigation with
real audio/playhead activity and are labeled accordingly. No camera/photodiode
is available. A post-walkthrough query on the unchanged normal image recorded
the cumulative controller histograms below. This normal image injects no
scripted touch or musical commands. No pixel-visible claim is made.

| Human-session software boundary | Count | p50 / p95 / p99 upper bounds (ms) | Exact max (ms) |
|---|---:|---|---:|
| Observation → dispatch | 123 | 5 / 26 / 52 | 53.464 |
| Dispatch/state update | 123 | 1 / 1 / 1 | 1.997 |
| Enqueue → observed audio acknowledgement | 25 | 20 / 51 / 52 | 51.962 |
| Redraw preparation | 154 | 5 / 71 / 80 | 79.947 |
| Present call | 154 | 1 / 3 / 9 | 19.220 |
| Full-page preparation | 19 | 71 / 80 / 80 | 79.947 |
| Controller observation → display submission | 44 | 38 / 77 / 81 | 80.601 |

The measured p95 submission upper bound **77 ms** passes the <120 ms target
for these eligible/coalesced samples. Poll duration p50/p95/p99 upper bounds
are 1/1/1 ms, maximum 1.079 ms; this excludes contact-to-poll waiting.
No per-widget/per-workflow timing or scanout measurement is inferred.

## 8. Display correctness and task boundaries

Continuous controls retain partial invalidation. Context changes force full
refresh on Track/source/sample/Project/Pattern changes as appropriate, including
sample replacement, transient apply and Project activation. Waveform data is
completed outside audio before publication, never scanned from PCM on redraw.
Full framebuffer clears prevent stale overlays during transitions. Source
inspection and tests do not certify absence of physical scanout artifacts.

Audio/storage ownership, immutable events, sample retirement and Project
snapshot/apply remain unchanged. No blocking SD/audio allocation or new wait
is introduced in touch dispatch. Histograms execute in touch/UI tasks only.

## 9. Physical qualification and human checklist

**HUMAN PHYSICAL UX ACCEPTANCE PASS (user report).** After final firmware
restoration, the user replied **“everything seems ok”** to the A–E walkthrough
and visual/touch/audio defect question. No defects were reported. This supports
ordinary workflow/legibility/responsiveness acceptance; it does not establish
deliberately induced physical controller faults, power loss or scanout timing.
No LCD photos or detailed subjective listening ratings were supplied.

The normal final image was restored and a ten-minute read-only serial capture
was completed ([controller log](m22/HUMAN_CONTROLLER_SERIAL.log)). It contains
zero dispatch/state-update/enqueue-ack/controller-to-submission observations:
no finger interaction was recorded. Polling samples and idle redraws are not
input latency evidence. The shared renderer label says
`mixed_real_and_scripted_redraws`; this normal-image session injected no touches
or musical commands. Only `U` queried existing software histograms. A–E remain
pending at that capture's end. The user's subsequent report supplies the
qualitative acceptance. A second read-only query archived
[post-walkthrough software counters](m22/HUMAN_POST_WALKTHROUGH_SERIAL.log)
with 123 controller events and 44 eligible submissions (section 7).

1. SEQ: select Track, toggle several steps rapidly, change tempo, EDIT STEP,
   velocity/probability, SEQ, bank A/B. Check white selection, yellow activity
   stripe and cyan ON fill remain distinguishable.
2. SAMPLE: load a real indexed WAV; wait for completion; Playback start/end;
   Slice VIEW, USE ACT, audition and release; Auto Analyze then Cancel;
   analyze again and Apply. Confirm magenta proposal was never shown as accepted.
3. PATTERN: inspect another Pattern while playing; Step and Basic/Tone/Sample
   locks; return to SEQ. Confirm Track/Pattern/Step context is correct.
4. PERFORM: Override, Fill, Mix mute/solo; hold each x2/x4/x8, drift outside,
   release, change tab and release; verify normal sequencing resumes. Edit Chain
   while stopped; cancel Delete/Clear confirmations.
5. PROJECT: Save As to a disposable name, wait for SAVED; edit, Save, Load,
   cancel New, confirm New, reload. Check errors/pending status and dirty star.
   Repeat editing while audio plays; report missed touches, clipping, artifacts,
   freezes or audio interruptions. Electrical power-removal recovery is separate.

## 10. Physical SD results

Fresh [SD serial evidence](m22/SD_SERIAL.log) passes `analyze_m22_sd.py`:
indexed WAV loads, replacement/retirement, invalid depth/truncation,
fragmentation/reserve rejection, Project Save/Load, CRC generation fallback
and restoration, three missing references/restoration, and rejected missing
Project preserving current state. Unmount, short-read and allocation failure
are injected failures, not physical card removal/power cutting. Reserved files:
M22_LAYOUT_QUAL / M22_MISSING_QUAL.

The isolated SD console derives from `6936720`, before final touch/pending-message
refinements; loader, DSP, ownership and codec match final source. It is controlled
by serial, not human UI. It recorded 24,816 Repeat hits and twelve Chain loops,
maximum 3,743 µs (35.5% reserve), zero errors and 21 loading/analysis overlap blocks.
p99=3,669 µs covers the first 16,384 of 17,999 measured blocks; maximum/error
counters cover all blocks. No p50/p95 or complete-interval p99 is invented.
The optional dense-component marker is absent; actual timing/hit records and
the passing overall SD gate are reported independently.

## 11. Fresh SAMPLE/SYNTH realtime results

Initial M22 source `6936720` passed both complete real-board runs, before the
additional controller-error cancellation/serial query. SAMPLE p50/p95/p99/max:
2,867/3,732/3,857/4,065 µs (30.0% reserve); SYNTH:
3,738/4,089/4,201/4,402 µs (24.2%). These initial captures are retained separately
and will not substitute for final-source qualification.

Final firmware source: `432abf2aa65857ec03a46e9d1a840c954607dc2a`.
Saved images were hash-verified before flash; production, Nearest, SAMPLE and
SYNTH all compiled successfully.

| Final real-device scripted stress | Blocks | p50 / p95 / p99 / max (µs) | Worst reserve |
|---|---:|---|---:|
| SAMPLE | 13,782 | 2,865 / 3,696 / 3,828 / 4,116 | 29.1% |
| SYNTH | 13,782 | 3,730 / 4,119 / 4,228 / 4,489 | 22.7% |

Both pass the original strict M20→M19→Repeat→Chain analyzer chain, with zero
misses, failures, timeouts, watchdogs or rails. SAMPLE free/largest/internal heap
remains 24,265,192 / 24,117,236 / 208,348 bytes; SYNTH remains
26,427,944 / 26,214,388 / 233,484. Histogram/packet additions cost about 9.3 KB
internal memory versus historical SAMPLE, with stable before/after heap and no
audio allocation.

One final SAMPLE attempt had max=4,116 µs and zero audio errors but **zero Chain
loops**; strict coverage rejected it. It is retained as [rejected capture](m22/REJECTED_SAMPLE_CHAIN_SERIAL.log).
The unchanged saved image was rerun and completed one loop. No analyzer was
weakened and the rejected attempt is not counted as a pass. Wall-clock coverage
variability remains separate from deterministic musical equivalence.

Final SAMPLE redraw p50/p95/p99 upper bounds: 26/95/124 ms; max 145.493 ms.
Full-page upper bounds: 85/129/140 ms. Present-call upper bounds: 1/5/15 ms,
max 20.987 ms. SYNTH full-page preparation upper bounds: 110/153/158 ms,
max 158.056 ms. Native partial slider/playhead transfers remain 49,152 / 24,576
bytes. Historical M21.4 full-page maxima were already 100–146 ms; reduced glyph
calls did not remove that limit. No universal renderer speedup is claimed.
Scripted captures have zero controller events, so input/ack quantiles are not
measured in those scripted captures. Human evidence is reported separately
in sections 7 and 9.

## 12. Musical event equivalence

The inherited 108-check baseline includes the complete original M21.4 A command
journal replay, canonical event/RNG/transport/voice state and Project bytes.
The final model, voice/sample transport, pitch, filters/Delay, slice/transient
algorithms and Project codec are unchanged. Identical intended commands retain
that equivalence. A wall-clock UI stress capture is not declared byte-identical
to an earlier capture; draw timing can change when scripted commands arrive.

## 13. Persistent compatibility and production configuration

Project remains **V2 / exactly 52,704 bytes**, with no new fields or migration.
Production stays interpolation=1, Linear32=1, magnitude=1, cache=0, placement=1.
Nearest legacy builds independently. New M22 stress environments select the
same production DSP; historical environments/flags remain intact.

## 14. CI and artifacts

The added M22 CI job builds production/fallback/SAMPLE/SYNTH, executes gesture,
font and screenshot checks, verifies production flags and runs strict inherited
analyzers against fresh M22 captures. Firmware ELF/BIN/MAP, build/package/source
hashes and raw serial capture archives are retained. The remote M22 job passed
for `5d60532cb3a44bbf12ca2c102df5caf7f1854684`
([Actions run 97](https://github.com/ovelhaaa/P4SDM/actions/runs/38087003883)).
All jobs on the final branch revision must succeed; the final remote gate is
tracked in [PR #17 checks](https://github.com/ovelhaaa/P4SDM/pull/17/checks).
Local results do not substitute for that remote gate.
The final local suite passed **115/115**, including fresh SAMPLE/SYNTH and SD
qualification. Added
screenshot assertions use real widget geometry to verify ON fill, yellow
activity stripe, selected white Step edges and selected Track fill; the
contact-sheet regression compares RGB pixels against checked-in evidence.

## 15. Known issues and remaining validation

The user reported no ordinary workflow, touch, visual or audio defects.
Deliberate physical controller-failure recovery, electrical power removal and
scanout measurement remain untested. Software latency is aggregated over
eligible/coalesced samples, not every packet or a detailed per-widget study.
Automated SD qualification uses existing generated fixtures independently of
the user's overall browser/Project walkthrough report. Stop after M22.

## 16. Acceptance classification

**TECHNICAL: PASS** host checks, command semantics, final firmware builds,
display diagnostics and real-device audio/SD stress. Remote acceptance also
requires all final PR checks to report SUCCESS as described above.
**PHYSICAL UX: PASS** from the user's overall A–E report, with real controller
software counters supporting responsiveness. **Overall: PASS once all final
remote Actions jobs report SUCCESS.** Visible-pixel latency and deliberate
physical fault tests are not claimed.
