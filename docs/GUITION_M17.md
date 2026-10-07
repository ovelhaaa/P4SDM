# M17 - Pattern Chain and project V2

Baseline: accepted M16 merge `dbc9505`. Chain is arrangement metadata above the unchanged Pattern structure and existing rational sample clock. No timeline, performance override, scenes, fills, automation or MIDI.

## Representation

`ChainEntry`: 2 bytes (Pattern 0..15, repeats 1..16). `PatternChain`: 66 bytes (32 entries, length 0..32, loop). `TransportMode`: one-byte PATTERN/CHAIN enum. Commands remain 12 bytes; `step` captures the explicit numerical row and `value` captures edit intent. Invalid/full insertions and invalid edits are rejected without redirecting the target. Engine is 52,160 bytes, +96 over M16; UI mirror grows equally. Project State is 53,192 bytes, +64 after alignment. V2 encoded buffer grows 67 bytes; requested PSRAM staging plus file is 105,896 bytes (+131). All allocations remain once at startup; audio methods allocate nothing.

## Playback algorithm and edits

Empty Chain means PATTERN. Adding entries never enables CHAIN. Mode changes require STOP; empty CHAIN falls back to PATTERN. PLAY clears the manual queue, starts entry zero/repeat zero/step zero, and retains selected editor Pattern. STOP->PLAY always restarts. Existing PLAY seed behavior remains; Chain transitions never reset playback or edit RNG.

At the existing next-step onset, the audible Pattern length determines wrap. Existing code cancels pending ratchets and releases sequenced Gates. CHAIN increments completed loops; if the current numerical entry exists and completed loops are below its current repeats, it repeats using that entry's current Pattern. Otherwise it advances one numerical index, resets completed loops, then accepts next entry, loops to zero, or stops with no extra trigger, no queued transition, and normal Gate release. The new Pattern is chosen before step zero; clock remainder is retained. There is no second clock or cached total entry duration. Length edits naturally change future wraps. All eight locks, slices, probability, ratchets, voices and global Track settings retain their original implementation.

The audible loop remains accepted until next wrap. Increasing repeats extends the entry; reducing below completed loops advances next time. Current-entry Pattern edits change the next repeat, or are skipped when repetitions are complete. Future entries use their current definition when reached. Insert/delete follow numerical indices under current array ordering, without entry identities. If the runtime index no longer exists, next wrap advances/end-loops/stops as above. Deleting the last entry or confirmed CLEAR falls back to PATTERN, clears the queue, and continues the accepted audible Pattern without a mid-loop jump. CLEAR preserves every Pattern and resets Chain definition/loop/mode.

Manual Pattern selection in CHAIN updates only selected_pattern. It never changes audible Pattern/Chain position or populates manual queue. PATTERN retains latest-queued-wins quantized switching. Runtime entry/repeat do not persist or mark dirty. All edit commands follow existing bounded UI-to-audio ownership/dirty revisions; harmless rejected/no-op musical requests retain M16's conservative dirty policy.

## UI and telemetry

FX -> CHAIN opens six 48-pixel rows with UP/DOWN scrolling, ADD after selected row (current editor Pattern, one repeat), DELETE, PATTERN -/+, REPEATS -/+, LOOP, MODE, CLEAR/CONFIRM/CANCEL, PLAY/STOP and BACK. `>` marks selected row; `*` marks audible row. Footer shows preferred mode and repetition progress. Row selection never automatically moves the Pattern editor, including while stopped. Empty mode remains PATTERN; mode switching is gated while running. Individual edits redraw the affected row/status; structural shifts and scrolling redraw the visible list. LOAD/NEW resets row/scroll/pending confirmation. Runtime row, repeat, mode and playing state reach the UI through bounded atomic publications.

Aggregated audio telemetry: starts, entry advances, repeat loops, full loops, clean stops, Chain Pattern switches, min/max lengths, all-row coverage and maximum boundary block. A final capture snapshot preserves these measurements; no per-transition Serial. UI telemetry counts page entry, edits, row selection and scrolling.

## V2 format and crash-resistant migration

The existing 24-byte explicit little-endian header remains. V2 payload starts with exactly the V1 52,613 bytes, then 67 bytes: length, loop, preferred mode, and 32 interleaved Pattern/repeat pairs. Payload **52,680**; file **52,704** bytes. Default CRC32: **2,419,985,824**. Inactive entries also encode and validate Pattern 0..15/repeats 1..16. Invalid length/boolean/mode, empty CHAIN mode, CRC, truncation and trailing bytes are rejected before destination mutation.

V1 reads migrate to empty Chain, loop OFF, PATTERN with every old field preserved. Saves write V2 only. Prefix inspection chooses version-specific payload size; future version detection precedes size enforcement. Load never rewrites V1. V1 A generation 10/missing B loads in RAM, then next save writes V2 B generation 11. Newest supported valid generation wins independent of version. Corrupt V2 falls back to V1 and vice versa. Future slots abort load/save even with a supported older slot. Existing write/flush/readback verification never overwrites newest valid copy.

Snapshot/apply remain 17 metadata blocks: block zero contains Chain/preferred mode with globals/Tracks; 16 Pattern blocks follow. The bounded Delay clear remains 87 additional blocks. Apply restores definition/mode STOPPED, runtime row/repeat zero, normal seeds and empty queue. NEW gives empty Chain/loop OFF/PATTERN.

## Host verification and CI

Chain tests cover 0..32 bounds, golden complete onset sequences, exact final-sample boundary, loop/nonloop STOP, lengths 1/3/7/12/16 and swing, 100 boundary cycles without phase drift, active/future edits, insertion/deletion, editor/queue separation, RNG continuity, accepted ratchet parents, touch targets and no-allocation guards. V2 suite retains M16's full field variation, all truncations, CRC-correct semantic corruption, recovery/save planning, runtime reset and 1,000 snapshot/apply transactions; it adds all 32 Chain entries and corruption of every inactive/active record.

Migration extracts actual accepted M16 model/codec from Git and generates varied Tracks/Patterns/locks/slices/references. V2's old-field prefix matches every V1 payload byte. Mixed V1/V1, V1/V2, V2/V1, corrupt supported slots, truncated writes, future prefixes and save planning pass. Actual M16-to-M17 PATTERN equivalence passes **3,600,000 sample positions**, varied seeds/swing/source/transitions and all eight locks. All historical CI host suites/analyzers/equivalence checks remain and pass locally. CI adds both firmware environments, Chain tests, actual M16 equivalence, actual V1 migration and M17 device analyzers.

## Realtime qualification

Both device stress modes retain the dense first 2,068 blocks: 16 voices, 240 BPM, four ratchets, all locks, Delay and display. Later stages use all 32 entries, repeated entries, short lengths 1/3/4 and inherited live length edits. Three RAM project roundtrips remain at 22/34/46 seconds; latter two snapshot/apply a nonempty Chain. Late UI exercises scrolling, delete/add, Pattern/repeat edits, loop toggles, and stopped mode toggles. Deadline: 5804.989 us; required worst-case headroom >=20%, zero misses. Measurements, hashes and CI links follow after final captures.

## Physical status and limits

**PHYSICAL V2 PROJECT MIGRATION / CHAIN PERSISTENCE PENDING.** No SD/FAT power-loss durability or musical power-cycle restoration is claimed. Host migration/recovery and device RAM roundtrips do not replace physical SD qualification. Physical touch usability, visual integrity and audible listening remain separate checks. Sample residency remains M16's per-Track model. Stop after M17; Performance Mode, scenes, fills and MIDI remain out of scope.

## Final device results

Implementation source: `768c48b` (tests/documentation follow-up does not change firmware). Genuine native USB captures on COM13, 10,337 blocks / 60.006 seconds each. Final SAMPLE/SYNTH pass `analyze_m17.py`, including every inherited assertion. Initial logs are retained but rejected: the first Chain page handler intercepted scripted inherited Tools actions. The handler was narrowed to its own control IDs, and the late non-loop section uses short lengths so natural STOP is reached before capture end. No inherited coverage/budget assertion was weakened.

| Firmware | Render p50 / p95 / p99 / max us | Min headroom | Boundary block max us | Snapshot max us | Apply/clear max us |
| --- | --- | --- | --- | --- | --- |
| SAMPLE | 2744 / 3454 / 3542 / 3756 | 35.30% | 3610 | 3468 | 2043 |
| SYNTH | 3626 / 4117 / 4215 / 4412 | 24.00% | 4259 | 4218 | 1956 |
| NORMAL | 1789 / 1809 / 1817 / 2004 | 65.48% | 0 | 0 | 0 |

Both modes have zero missed deadlines, I2S failures/timeouts and rails; three successful RAM V2 fixtures, zero fixture errors, 51 snapshot blocks and 312 apply/clear blocks. No Chain bookkeeping is performed for ordinary audio samples inside a Pattern. Maximum boundary block measures the entire audio block, including normal rendering and command work, rather than an isolated algorithm estimate.

SAMPLE: 5 starts, 120 entry advances, 17 repeat loops, 1 full loop, 1 natural stop, 119 Chain Pattern switches, lengths 1..16. SYNTH: 5 starts, 185 advances, 25 repeats, 3 full loops, 1 natural stop, 183 switches, lengths 1..6. Both visit all 32 rows (`0xFFFFFFFF`), and UI records 18 page entries, 10 edits, 9 row changes and 2 scrolls. Runtime counters survive project fixture applies for aggregate qualification but are never persisted.

| Mode | Internal free before -> after | PSRAM free before -> after | Largest PSRAM block before -> after |
| --- | --- | --- | --- |
| SAMPLE | 273816 -> 273816 | 28616312 -> 28616312 | 28311540 -> 28311540 |
| SYNTH | 298676 -> 298676 | 30779004 -> 30779004 | 30408692 -> 30408692 |
| NORMAL | 298724 -> 298724 | 30779004 -> 30779004 | 30408692 -> 30408692 |

Compared with accepted M16, SAMPLE/SYNTH internal free are each 400 bytes lower; PSRAM allocator buckets retain the same free/largest values despite 131 additional requested staging bytes. Normal linked RAM grows 64 bytes (78,400); complete Engine/mirror sizes and runtime heap deltas are reported separately. No ongoing allocation/heap growth is observed.

SHA-256 of final binaries:

- `guition_chain_stress`: `e5fe68806ff8f8062742cbb2c284a3da5ed465eaf60d7810daabee0a039fc03b`
- `guition_chain_synth_stress`: `b0e1772280b20ece0b073e7232210394d77dc9d6911fabc1f7b63c84aa086cf1`

- `guition_app`: `803770518946c8bcc6c4e4b952f16f26244acc1c2a1e07e384fb4243007ac1da`

Normal no-card firmware is restored on the board. It passes the normal analyzer: Chain empty/PATTERN defaults, zero Chain starts/transitions, zero project fixtures/snapshot/apply/dirty state, zero PCM/cache/lock residue and zero nonzero output. Internal free is 384 bytes below M16 normal; PSRAM free/largest remain unchanged. All normal heaps stay fixed through the capture.

## Changed files and acceptance answer

- `src/app/model.h`: fixed Chain, explicit commands/mode, wrap scheduling, row geometry/hit testing.
- `src/app/project.h`, `projects.cpp`: canonical V2 state, strict codec/version-specific reads, snapshot/apply and mixed-version slots.
- `src/app_main.cpp`: dedicated UI, runtime publication, aggregated telemetry, retained dense workload and Chain qualification script.
- `platformio.ini`, `.github/workflows/guition.yml`: two M17 stress environments and retained/extended CI.
- `tests/chain_test.cpp`, `no_chain_equivalence.cpp/.py`, `project_migration.cpp/.py`, `project_test.cpp`, `analyze_m17.py`: host, actual Git baseline, migration, corruption/recovery and device qualification.
- `tests/analyze_m15.py`: configurable Engine size with unchanged historical default and workload assertions.
- `README.md`, this report and genuine serial captures: workflow, measurements and limitations.

**Can P4SDM now arrange and play a complete multi-Pattern structure deterministically while preserving the existing Pattern engine, realtime guarantees and M16 crash-resistant project model?** Yes, for the bounded 32-entry arrangement: host golden/equivalence/migration/recovery tests and genuine SAMPLE/SYNTH captures demonstrate deterministic boundaries, retained Pattern behavior and >=20% realtime headroom. Crash resistance retains M16's verified dual-generation file protocol. Actual physical V2 migration, sample restoration/power-cycle, card/FAT durability, touch usability and audible listening remain pending and are not claimed.

## Build and CI status

All **17 local PlatformIO environments** pass, retaining every M3-M16 target and adding `guition_chain_stress` / `guition_chain_synth_stress`. All workflow host commands and historical analyzers/equivalence comparisons pass locally; final M17 SAMPLE/SYNTH/NORMAL analyzers also pass. The firmware source is frozen at implementation commit `768c48b`; the qualification follow-up adds the report, original/final captures, README and additional host assertions without changing that firmware.

Remote pull-request CI is verified separately after publication; its run link/result is recorded in the delivery message. Physical SD status remains pending regardless of CI.
