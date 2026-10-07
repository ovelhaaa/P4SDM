# M16 — Project Save/Load + Versioned Persistence

M15 was merged through PR #4 into `main` at `ef6f7e792544b2c3566a3718a8c08f774928df19`, after both accepted-head pipelines passed. M16 starts from that merge on `codex/m16-project-persistence`.

**PHYSICAL PROJECT SAVE/LOAD PENDING.** The connected board reports `NO CARD / MOUNT ERROR`. Host corruption/recovery and genuine device RAM serialization/boundary qualification do not establish FAT durability, audible quality, physical touch usability, or power-cycle restoration on an actual SD card.

## Commits and scope

- `3862514`: canonical model/codec, dual slots, storage integration, bounded snapshot/apply, sample retirement/restoration, project UI, host tests and qualification environments.
- `adec077`: reject any future-version slot before choosing an older supported fallback, preserving the current project and future-format files.
- Evidence/report/CI commit: the commit containing this document and final captures.

Changed implementation: `src/app/project.h`, `projects.h/.cpp`, `project_ui.h`, `samples.h/.cpp`, `model.h`, `app_main.cpp`; environments in `platformio.ini`; `tests/project_test.cpp`, `no_project_equivalence.py/.cpp`, `analyze_m16.py`; CI, README, this report and genuine serial logs. Existing PCM/sampler render, WAV decoder, SliceBank, clock, accepted-parent resolution, synth and FX processing algorithms remain unchanged. Unavailable SAMPLE triggers explicitly stay silent instead of starting a synth voice.

## V1 disk format

One central `project::version = 1`. Every field is written individually using explicit u8, u16 LE and u32 LE helpers. No raw Engine/Track/Pattern memory, native bool representation, pointer, compiler padding or timestamp is written. Pan is encoded as unsigned `pan + 127` (0..254).

| Header offset | Bytes | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `P4PR` |
| 4 | 2 | version, 1 |
| 6 | 2 | header size, 24 |
| 8 | 4 | payload size, 52,613 |
| 12 | 4 | generation |
| 16 | 4 | CRC32 of the entire payload |
| 20 | 4 | reserved, must be zero |

File size is exactly **52,637 bytes**. V1 rejects extra trailing bytes and every truncated file. Version zero is unsupported; any slot with a future version reports `PROJECT VERSION TOO NEW` and prevents load/save from reinterpreting or overwriting it. The prefix is inspected before enforcing the V1 total size, so differently sized future files receive the proper version error.

Payload order: 37 global bytes; 16 Track records of 181 bytes (21 controls, 64 slice-coordinate bytes, 96 reference bytes); 16 Pattern records of 3,105 bytes. Each Pattern has length, 16 little-endian step masks, then Track-major/step-major records of 3 StepMeta bytes and all 9 StepLocks bytes. All eight lock bits and their stored values are preserved even when disabled or suppressed by source. Every inactive step and every inactive slice slot is retained, with no hidden normalization of historical musical data.

Global fields: zero-padded 32-byte project name (maximum 31 visible characters), BPM, swing, Delay enable and selected Pattern. Every Track persists source, mute, volume, pan, separate synth/sample pitch, synth envelope length, wave, cutoff, resonance, Delay send, normalized START/END, reverse, OneShot/Gate, choke, slice count/selected/enabled, all 16 regions, and remembered sample basename. Solo is temporary and excluded. Current pitch is derived from the source; `sample_configured` is reconstructed on decode so the first restored assignment cannot reset saved controls.

CRC uses reflected IEEE CRC32, polynomial `0xEDB88320`, initial/final XOR `0xFFFFFFFF`; the known `123456789` vector is `0xCBF43926`. Golden default payload CRC is **170069039**. Identical canonical state/generation gives identical bytes; changing generation changes only the header generation field.

CRC is checked before a two-pass decode: the first pass validates every field without writing the destination; only a fully valid payload is committed in the second pass. A failed parse, unsupported version, CRC, size, semantic error or mismatched directory/name never reaches the audio handoff and leaves the live project/dirty revision untouched.

Semantic policy is strict rejection: BPM 30..400; swing 50..75; Pattern length 1..16; velocity 1..127; probability 0..100; ratchets 1..4; pitches/volume/tone/send/length 0..127; pan -127..127; wave/slice lock 0..15; source/reverse/mute/enabled/delay 0/1; mode 0/1; choke 0..8; slice count 1..16, selected < count, and START < END in every active/inactive slice region. Track playback START and END remain independent normalized u16 values: crossed/equal coordinates are legal M13 edit intent, already defended by `resolve_region`, and are saved exactly. Sample names are terminated basenames within 96 bytes; control characters, separators, drive colon and `..` are rejected. Project names allow only letters, digits, space, underscore and hyphen, with no leading/trailing spaces. Strings after the terminator must be zero on disk.

## Two-generation storage

Files live at `/P4SDM/PROJECTS/<NAME>/A.P4P` and `B.P4P`; sample references resolve under `/P4SDM/SAMPLES/`. The same bounded generation planner is used by production and recovery tests. Generation comparison uses unsigned serial-number arithmetic, including `0xFFFFFFFF → 0`; equal generations choose A deterministically. A generation separation of exactly half the u32 range is ambiguous and chooses A.

Save inspects both files, selects the newest valid slot, and targets the other slot. Missing/bad older slots can be recreated. Read I/O failures and future-version slots abort rather than risking an unreadable good copy. It checks free capacity for one complete file plus 64 KiB, creates directories, removes/truncates only the non-newest slot, writes all bytes, flushes/closes, and reads back the header, complete CRC/semantic validation, generation and expected CRC. Only verification marks the snapshot revision saved. It never removes the newest valid slot and does not rely on FAT rename atomicity.

Load validates both slots and chooses the newest valid supported generation; corrupt/truncated new slots fall back to the older valid copy. A future slot is explicitly rejected even if an older V1 copy exists. A final reread validates the chosen slot before decode/apply. Removal during metadata reads keeps the current project; removal during sample restoration produces missing Tracks after metadata activation. On an explicit operation after insertion/removal, the storage task may remount; there is no periodic project save or autosave.

This provides best-effort crash resistance at the application/file-validation level. FAT metadata and SD-controller power-loss behavior remain physical qualification items; two files on one FAT volume cannot guarantee survival of arbitrary filesystem/card failure.

## Realtime ownership and memory

The existing `sample_storage` task owns project directory scans, validation, encode/decode, file reads/writes, flush/verify, sample retirement and sequential restoration. No filesystem, serialization, String, allocation, or per-field Serial executes in render, trigger, sequencer sample clock, or audio boundary copy.

Project staging is **53,128 bytes** in PSRAM; encoded file buffer is **52,637 bytes** in PSRAM: 105,765 requested bytes, plus allocator overhead. Both are allocated once before audio/UI baselines. Browser is fixed 32 names × 32 bytes, scanning at most 128 directory entries. Audio remembered references use 16 × 96 fixed bytes; bounded retirement pointer arrays are fixed. Engine remains 52,064 bytes, its UI mirror another 52,064; Track 116, Pattern 3,106, StepLocks 9, accepted parent 20 bytes. No extra internal Engine-sized allocation was added.

Save uses a coherent **17-block** snapshot: globals/Tracks/references on block zero, one whole Pattern per subsequent block. Musical commands and pad consumption are frozen during this ~99 ms window, while the existing transport, voices, clock, RNG and audio render continue. Pending edits are consumed after release, so no field set can straddle different musical revisions. The audio-owned revision is captured with the snapshot. Edits during file I/O advance the live revision and remain dirty even after that older snapshot verifies successfully. UI never reads mutable Engine state for persistence.

Apply also uses 17 bounded metadata blocks. Block zero stops transport, cancels ratchets, clears solo, resets probability/edit seeds, phase/head and queue, stops sample/synth voices, clears filter and voice override histories, and detaches active sample ownership/previews for a real LOAD/NEW. It then applies one Pattern per block. Delay processing is disabled and both public delay buffers are cleared in chunks of 1,024 samples per channel per block (87 blocks for 88,200 samples), before resetting the write index and restoring Delay enable. Thus each complete apply takes **104 blocks** (~604 ms), never a blind 352,800-byte clear in one realtime block. Old queued commands/pads are discarded with UI command acknowledgements; stale input/capture/tool intent is reset. Audio stays stopped and play/edit actions remain gated until sample resolution finishes.

Selected and playing Pattern become the saved selected Pattern; queued Pattern is none and transport is STOPPED. The RNG uses the established playback default and edit seed `0x4D395031`. PCM pointers, waveform caches, active Voices, filter/Delay tails, voice overrides/coefficient caches, ratchets, clock phase, transport running, RNG history, UI flags, queues and Transfer addresses are excluded from the format.

## Sample restoration and lifecycle

The authoritative filename is remembered separately from waveform preview and PCM; ordinary publication records it on the audio boundary. A load restores every nonempty reference sequentially, including references on SYNTH Tracks, using the established bounded WAV parser, PSRAM budget, one-slot Transfer publication and audio acknowledgement. Restoration preserves source/pitch/playback/slice controls rather than invoking the ordinary first-assignment default/source switch. Before freeing any old allocation, audio has detached voices/Transfer.active and cleared preview pointers under the preview lock; only the storage task frees PCM/metadata it owns.

A missing/rejected WAV keeps its saved reference and source, produces no PCM, and is displayed as `MISSING SAMPLE Txx`. SAMPLE remains silent; it does not become SYNTH. Failures advance the bounded queue and end in `PROJECT READY / n MISSING`. Progress is `LOADING SAMPLES n/total`. Duplicate paths keep one PCM allocation per Track, matching M15: 16 copies can use 16× the file's decoded mono size. No deduplication, embedding, copying or residency redesign was introduced.

Qualification RAM fixtures retain their separately owned resident PCM only for metadata roundtrip testing; they are not pretended to be restored from SD. Real LOAD/NEW uses retirement and WAV rereads. Physical sample replacement, missing-file recovery after reinsertion, retirement/read errors and card durability need the SD checklist below.

## Touch workflow and dirty semantics

FX → PROJECT opens current name and `*` unsaved indicator. Home provides SAVE, SAVE AS, LOAD, NEW and BACK. SAVE writes the current named project without confirmation; UNTITLED routes to SAVE AS. SAVE AS offers generated names `PROJECT_0001`..`PROJECT_9999` with Previous/Next and a deliberate confirmation of the captured name (including every possible overwrite). No PC or general-purpose keyboard is required. LOAD has a bounded browser with Previous/Next and Load Selected. Dirty LOAD/NEW requires Confirm/Cancel with captured intent. NEW works without SD, stops/clears musical/runtime state, retires samples, resets controls/BPM/slices, and becomes clean UNTITLED.

Dirty tracking is conservative: accepted musical commands, selected Pattern, base/lock/slice/source edits and ordinary sample assignments increment the audio-owned revision, including harmless no-op musical requests. Play, audition, gate release, temporary solo and hidden edit-seed reroll do not mark dirty. Verified save clears only the captured revision; successful LOAD/NEW becomes clean. Failure leaves dirty unchanged. There is no autosave. Messages distinguish NO SD/NO PROJECTS, bad header/size, CRC, unsupported/future versions, invalid state, directory/write/verify failures, insufficient space and missing samples; failed load explicitly says the current project was kept.

## Host and device verification

`project_test.cpp` deliberately varies all 16 Tracks/Patterns, every StepMeta range, all 256 lock masks and every stored lock field, all slice slots, source/tone/playback state, references, BPM/swing. It checks field-by-field equality, deterministic re-encoding and golden default bytes/CRC. It checks every truncation length 0..52,636, trailing bytes, malformed header/version/CRC, CRC-correct invalid enums/ranges/counts/selection/regions/step metadata/locks/names/termination, path traversal, old-copy recovery at every truncation, corrupt/zero-tail new files, single/no valid slots, equality and generation wrap. The shared save planner is exercised at representative interrupted-write boundaries and preserves the prior byte vector unchanged. Runtime apply resets and 1,000 full snapshot/apply transactions execute with `new` forbidden. Touch targets and confirmation/name intent are checked.

All 18 C++ host suites pass. All historical Python capture analyzers and seven previous equivalence/FX comparisons pass; the additional actual M15→M16 idle-persistence comparison passes **3,600,000 sample-clock positions with all eight locks**, varied seeds/swing/source/Pattern transitions/RNG/ratchets. The comparator reads accepted M15 from Git, rather than reconstructing a claimed baseline.

Device SAMPLE/SYNTH stress each retains the accepted initial dense 2,068-block section (16 voices, 240 BPM, four ratchets) and inherited later UI/command exercises. Three no-SD RAM fixtures capture while playing, encode/decode in storage, apply/reset/clear Delay in audio and resume via explicit qualification commands. Fixtures are scheduled at 22/34/46 seconds, away from the inherited transition probes. Each capture has 10,337 blocks (60.006 seconds), aggregate telemetry only and stable before/after heaps. Final metrics and hashes follow below.

Initial retained captures: SAMPLE initial passed audio/coverage; SYNTH initial met realtime budgets but was rejected because fixtures at 16/28/40 seconds canceled the inherited short-to-long transition. The test schedule was corrected, not the inherited assertions. Initial logs are retained separately and are not final evidence.

| Mode | Render p50 / p95 / p99 / max µs | Minimum headroom | Snapshot block max µs | Apply/clear block max µs |
| --- | --- | --- | --- | --- |
| SAMPLE | 2982 / 3315 / 3397 / 3646 | 37.19% | 3401 | 2035 |
| SYNTH | 3331 / 4104 / 4220 / 4518 | 22.17% | 4214 | 1945 |
| NORMAL | 1790 / 1813 / 1820 / 1939 | 66.60% | 0 | 0 |

All three: zero missed deadlines, I2S failures/timeouts and rails. SAMPLE/SYNTH each record exactly 51 snapshot and 312 apply/clear blocks, three successful roundtrips and zero fixture errors. Normal records zero snapshot/apply/fixtures, zero dirty state, zero nonzero output, and no sample/cache/lock fixture residue. Transport is temporarily stopped by project apply, so whole-run active_min can be zero; the unchanged first dense section still has 16/16 active voices for 2,068 blocks, verified by the inherited analyzer.

| Mode | Internal free before → after | PSRAM free before → after | Largest PSRAM block before → after |
| --- | --- | --- | --- |
| SAMPLE | 274216 → 274216 | 28616312 → 28616312 | 28311540 → 28311540 |
| SYNTH | 299076 → 299076 | 30779004 → 30779004 | 30408692 → 30408692 |
| NORMAL | 299108 → 299108 | 30779004 → 30779004 | 30408692 → 30408692 |

Normal linked PlatformIO summary: RAM 78,336 bytes, flash 671,428 bytes, reported separately from complete C++ object sizes and runtime heap measurements. Compared with accepted M15 normal, runtime internal free is 3,040 bytes lower and PSRAM free 106,504 bytes lower (the fixed staging allocations plus boot/allocator overhead); both remain unchanged throughout the capture. The linker map places fixed browser/references and Engine/mirror in its static RAM regions; explicit project staging/file allocations use PSRAM. No SD/PCM fixture was added to normal.

Final firmware binaries were built from implementation revision `adec077ece03a30e1067fe912fe7ced5567203aa` and captured over native USB on COM13. SHA-256:

- `guition_project_stress`: `2a261ad4d050b2d5324c5572f79bba82adafb1a3f8e0511a01b6ca93e32a529b`
- `guition_project_synth_stress`: `8f0e63f599eea5a1b9323ec538b94dc8e0da34cdb3622fe8ff90bdf6ce72269d`
- `guition_app`: `922c7f6da4eadef44b58abeb0f56f93c722ad4f9b2d287bb2b909c4ec023b027`

Each was uploaded, reset and captured with `tests/capture_guition.py`; final logs end in `[M5 memory]` and pass `tests/analyze_m16.py` with the appropriate SAMPLE/SYNTH/normal mode. Normal `guition_app` is restored on the board. The retained `PRE_FINAL` captures also passed but precede the final future-version/read-path source freeze; they are not substituted for the final hashes/logs. All 15 local firmware environments pass; all 18 host suites and historical Python/equivalence checks pass.


## CI and physical follow-up

CI retains M3–M15 builds/tests/analyzers and adds both M16 firmware modes, V1 roundtrip/corruption/recovery/no-allocation/UI tests, actual M15 idle equivalence and final M16 SAMPLE/SYNTH/normal analyzers. It never depends on physical SD.

Remote CI: awaiting the evidence-head PR/push pipelines; this line will be updated with verified run links.

Physical checklist remains pending: format an SD with `/P4SDM/SAMPLES`, assign WAVs, save while playing, save again, power-cycle and restore all musical fields; test SYNTH remembered references, missing/rejected WAVs, duplicate residency, all locks/slices, overwrite/dirty confirmation and NEW retirement; corrupt/truncate the newest slot and verify fallback; remove power/card during header/payload/flush/readback and load/sample resolution; verify real free-space failures, playable output and touch usability. Preserve genuine captures for these checks.

**Can an M16 project be safely saved, power-cycled and restored without serializing runtime DSP state or risking loss of the previous valid project if a write is interrupted?** The implemented format excludes runtime DSP state, and the verified dual-slot protocol preserves the prior valid file through simulated interrupted writes, with measured realtime headroom. Actual power-cycle restoration and FAT/SD durability are **PHYSICAL PROJECT SAVE/LOAD PENDING**; they are not claimed as validated. Arbitrary filesystem/card corruption can still affect both slots.

Stop at M16. Autosave, sync, MIDI, song mode, undo, sample embedding/copying/deduplication, transient detection and time-stretch remain out of scope.
