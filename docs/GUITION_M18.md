# M18 — Performance Mode v1

Accepted baseline: M17 merge `589e718`. M18 adds a runtime layer above the existing Pattern/Chain boundary resolver. It uses the same Pattern bank, rational clock, trigger/ratchet/probability/lock/slice/Gate engine and audio renderer. No Beat Repeat, stutter, scenes, macros, MIDI, morphing, crossfade, snapshots, automation or project-format changes.

## Runtime representation and ownership

`PerformanceState` is 10 bytes: two 16-bit mixer masks; signed one-byte Override target/accepted target, pending/active Fill, return Pattern (`-1` = absent); and a return-at-end flag. `return_pattern >= 0` identifies ownership of playback. Chain entry/repeat remain in the existing Engine fields and freeze during ownership. No heap or Pattern copy is used. Separate 52-byte aggregate counters plus alignment make Engine **52,224 bytes**, **+64** over M17; the UI Engine mirror grows equally. `Ui` is **116 bytes**, **+4**. Audio publishes performance, mix, transport and command acknowledgement in four bounded atomic words guarded by a generation word: **20 bytes**. UI makes one bounded acquisition attempt per iteration, accepts coherent acknowledged snapshots, and never spins on audio. The transport word includes audible/queued Pattern and Chain context from the same publication.

The boundary resolver only changes the Pattern index and runtime bookkeeping. It runs at the existing Pattern wrap, after existing ratchet cancellation and sequenced Gate releases, before the usual step-zero events. No second clock, renderer, sequencer or audio event path exists. Ordinary samples do not resolve performance scheduling. Effective sequence eligibility adds the mixer mask check to the established pre-probability gate.

## Fixed semantics

- Priority is **Fill > Override > Arrangement**.
- Override commands contain explicit Pattern numbers. While running, latest pending target wins. Launch and active target changes take effect at the next existing Pattern boundary. The accepted Pattern repeats until cancelled. Cancel before acceptance causes no excursion; cancel after acceptance finishes the current loop and returns at its boundary. The original return context survives target changes.
- Fill commands also contain explicit Pattern numbers. Latest pending Fill wins. An accepted Fill plays one complete loop of its current Pattern length. Requests while Fill is active are ignored. CANCEL F clears a pending request; an accepted Fill completes. Fill returns to a still-requested Override, or directly to arrangement after Override cancellation. A pending Override can be accepted after a Fill as well.
- PATTERN captures the audible arrangement Pattern at acceptance, independently of `selected_pattern`. The editor may change selection during the excursion. The ordinary Pattern queue is cleared at acceptance; normal selection requests update editing selection but cannot queue playback while a performance request/ownership is present. Cancelled requests that never accept leave an existing normal queue intact. With performance inactive, M17 queue behavior is unchanged.
- CHAIN first accounts for the arrangement loop that just completed, using the existing Chain progression function. It saves the resulting next Pattern and leaves entry/repeat frozen at that next unconsumed loop. Temporary loops consume no Chain repeats. Return plays that saved loop, without a second advance. Example `P01 x2 -> P02 x2`: request during first P01 gives `P01, P08, P08..., cancel, P01, P02, P02`; Fill gives `P01, P10, P01, P02, P02`. Numerical index behavior for intentional canonical Chain edits remains M17's policy. An excursion requested during the final finite-chain loop is allowed to finish, then returns to the terminal state and stops without an extra arrangement trigger.
- Pattern lengths and live edits use existing Pattern references. No Pattern snapshot is made. Swing, accepted ratchets, all eight locks, Slice Lock, probability and Gate releases follow the existing engine. Performance transitions never reseed RNG.
- Mixer eligibility: `!Track.muted && !(performance.mutes & bit) && (!(canonical_solos | performance.solos) || ((canonical_solos | performance.solos) & bit))`. Either solo mask selects a Track, but neither can defeat either mute. Canonical mute/solo fields are untouched. These masks gate future sequencer parents before probability, locks, trigger and choke; they do not destroy an existing voice or Delay tail and do not suppress manual audition.
- STOP (including natural finite Chain STOP), LOAD and NEW clear all performance state and masks. PLAY begins with neutral performance state and canonical transport semantics. Masks otherwise survive Pattern boundaries. CLEAR MIX only clears the temporary mixer masks. CANCEL O and CANCEL F have explicit separate meanings. Pattern requests while stopped are ignored; mixer controls can be prepared while stopped but PLAY resets them.

## Persistence and dirty state

Performance commands are excluded from `project::musical`; temporary operations cannot dirty the project. Deliberate Pattern/Track/Chain edits retain normal dirty behavior. Snapshot copies only canonical fields. Apply block zero clears performance. No disk fields or codec layout changed: V2 payload **52,680 bytes**, file **52,704 bytes**, State **53,192 bytes**. The accepted M17 codec/model are extracted from Git for byte comparisons; varied canonical Tracks, Patterns, all locks and Chain metadata encode byte-identically even when all M18 runtime fields are populated. Existing M16->V2 migration and dual-generation recovery checks remain unchanged.

## UI

FX has PERFORMANCE beside CHAIN. Main navigation reaches it through FX. Chain's footer offers PERF when no CLEAR confirmation is pending; during confirmation that target explicitly says CANCEL. BACK returns to FX without cancelling a live excursion.

PATTERNS has sixteen 184x60 pads and explicit OVERRIDE / FILL NEXT selection. FILL NEXT applies to one pad tap and then disarms. `E`/left border marks editor selection, `A` marks arrangement, `O?`/`F?` marks requests, `O`/`F` marks accepted runtime state, and cyan horizontal borders identify the audible Pattern. Fill uses yellow text, Override cyan. The status line separates ARR and HEAR and shows frozen Chain entry/repeat. MIXER uses the same sixteen cells, with MUTE or SOLO mode. `B` and a red base-mute stripe identify persistent mute, `S` identifies existing solo, `M` and `+S` identify temporary masks; disabled tracks are grey. A visible legend explains these markers. CLEAR MIX does not unmute a base-muted Track. Cancellation, BACK and PLAY/STOP have independent targets. Subpage changes redraw the page; edits/publications redraw a fixed bounded set of cells. Load/new disarms the local Fill modifier along with the runtime reset.

## Host verification

`performance_test.cpp` runs under a global allocation guard: all 16 Tracks and 64 mute/solo combinations each, latest/cancelled Override and Fill requests, active target replacement, Pattern editor separation and queue ownership, Fill during Override and cancellation during Fill, one-loop lengths 1/3/7/12/16 with swing 50/60/75, no RNG reseeding, pre-probability suppression, all lock/Slice Lock event resolution with accepted ratchet children and Gate callbacks, every entry/repeat in multiple three-entry Chains with repeats 1/2/4/16, finite-chain terminal excursions, STOP/load/new, project byte identity and explicit dirty classification, plus large nonoverlapping touch targets. Another **3,000,000 active-performance sample positions** compare Override/Fill/return against ordinary Pattern-queue playback of the same audible sequence, including all event fields, probability RNG, ratchets, clock phase and step across all five lengths and three swings.

`no_performance_equivalence.py/.cpp` extracts actual M17 `589e718` sources. **7,200,000** deterministic sample positions compare PATTERN and CHAIN onset events (including all eight locks, slices and ratchets), clock phase, RNG, runtime Pattern/queue, Chain entry/repeat and playing state, across three seeds and three swings. It also compares complete V2 project bytes with performance state present only on M18. All **59** inherited workflow host commands pass locally. CI retains M3–M17 builds/tests/analyzers and adds both M18 builds, the host performance suite, actual M17 equivalence/V2-byte test, and SAMPLE/SYNTH/NORMAL M18 analyzers.

## Realtime qualification

Both M18 targets extend the M17 stress targets directly. The first **2,068 blocks** retain 16 voices, 240 BPM, four ratchets, all locks, Delay and display. All inherited sample fixtures, tone/lock/slice edits, Chain traversal/edits and three project RAM roundtrips remain. At 36–45 seconds, performance commands exercise latest Override replacement, multiple active loops, Fill during Override, both return paths, cancel, all 16 mutes/solos, rapid toggle/clear and short Patterns. There is no per-action Serial logging.

Counters report Override requests/accepts/loops/cancels, arrangement returns, Fill requests/accepts/completions, Fill-to-Override and Fill-to-arrangement returns, mute/solo edits, performance boundaries and worst full audio block containing a performance boundary. UI counters report page entries, pad interactions, Fill requests, mixer toggles and subpage switches. Final runtime state is captured alongside aggregate counters. Deadline **5,804.989 us**, required maximum **4,643.991 us** (>=20% headroom), zero misses. Genuine final device measurements are recorded below after qualification.

## Physical status

**PHYSICAL V2 PROJECT MIGRATION / CHAIN PERSISTENCE PENDING.** Genuine native USB firmware qualification and RAM project roundtrips do not establish SD/FAT write durability, power-loss recovery or sample restoration. No card is mounted in these runs. Manual physical touch usability, visual inspection and audible listening are not claimed. Host tests establish scheduling/event semantics; automated device scripts establish realtime workload coverage.

## Changed files

- `src/app/model.h`: fixed performance state/counters, explicit commands, central wrap resolver, mixer gate and page geometry/hit testing.
- `src/app/project.h`: runtime reset and non-musical classification; codec unchanged.
- `src/app_main.cpp`: dedicated page, coherent acknowledged publication, counters and inherited-workload performance phase.
- `platformio.ini`, `.github/workflows/guition.yml`: M18 environments and retained/extended CI.
- `tests/performance_test.cpp`, `no_performance_equivalence.cpp/.py`, `analyze_m18.py`: host, actual baseline/V2 and genuine-device checks.
- `tests/analyze_m17.py`: configurable Engine-size argument with unchanged historical default and workload assertions.
- `README.md`, this report and genuine serial captures: usage, evidence and limitations.

## Acceptance answer

Can P4SDM now temporarily divert PATTERN/CHAIN playback for live Override and Fill performance, then return deterministically to the untouched arrangement without changing persistent project state or the realtime Pattern engine? **Yes. Host event/clock/RNG comparisons, actual M17/V2 byte comparisons and genuine SAMPLE/SYNTH captures prove the bounded runtime diversion/return semantics with >=20% realtime headroom; normal firmware is restored and verified.** Physical SD durability and manual touch/listening remain pending. Work stops at M18.

## Final genuine device evidence

Firmware source is frozen at implementation commit `9dc8f58`; the evidence/test follow-up does not change firmware. All three accepted native USB captures use COM13 and contain **10,337 audio blocks / 60.006 seconds**. Every final log passes `analyze_m18.py`, which calls all inherited M17/M15 and earlier workload assertions. No budget or coverage assertion was weakened.

| Mode | Render p50 / p95 / p99 / max us | Min headroom | Performance boundary block max us | Chain boundary block max us | Snapshot max us | Apply/clear max us |
| --- | --- | --- | --- | --- | --- | --- |
| SAMPLE | 2703 / 3341 / 3436 / 3620 | 37.64% | 2781 | 3444 | 3453 | 2013 |
| SYNTH | 3740 / 4089 / 4183 / 4406 | 24.10% | 4181 | 4293 | 4148 | 1799 |
| NORMAL | 1756 / 1779 / 1785 / 1979 | 65.91% | 0 | 0 | 0 | 0 |

Both stress modes have zero deadline misses, I2S failures/timeouts and rails. All final runtime performance fields are neutral. Both retain 2,068 dense blocks, all 32 Chain-row coverage, three successful RAM V2 fixtures, 51 snapshot blocks, 312 apply/clear blocks and zero fixture errors. All-lock/ratchet/slice, inherited UI edit/dirty-render and heap assertions pass. Performance boundary measurement is the complete audio block, including ordinary DSP and command work, rather than an isolated algorithm estimate.

| Mode | Override request / accept / repeat / cancel | Fill request / accept / complete | Fill -> Override / arrangement | Arrangement returns | Mute / solo edits | Performance boundaries |
| --- | --- | --- | --- | --- | --- | --- |
| SAMPLE | 5 / 2 / 5 / 4 | 5 / 4 / 4 | 2 / 2 | 3 | 16 / 48 | 14 |
| SYNTH | 5 / 3 / 9 / 4 | 5 / 4 / 4 | 2 / 2 | 3 | 16 / 48 | 19 |
| NORMAL | 0 / 0 / 0 / 0 | 0 / 0 / 0 | 0 / 0 | 0 | 0 / 0 | 0 |

Each stress run records 13 Performance page entries, seven pad interactions, three UI Fill requests, 64 mixer toggles and one subpage switch. Other direct audio commands exercise replacement/cancellation and ignored/pending Fill slots. No per-action Serial logging is used.

| Mode | Internal free before -> after | PSRAM free before -> after | Largest PSRAM block before -> after |
| --- | --- | --- | --- |
| SAMPLE | 273528 -> 273528 | 28616312 -> 28616312 | 28311540 -> 28311540 |
| SYNTH | 298388 -> 298388 | 30779004 -> 30779004 | 30408692 -> 30408692 |
| NORMAL | 298452 -> 298452 | 30779004 -> 30779004 | 30408692 -> 30408692 |

Compared with accepted M17, SAMPLE/SYNTH internal free are each **288 bytes lower**; NORMAL is **272 bytes lower**. PSRAM free/largest remain identical to M17. There is no ongoing heap growth. The normal PlatformIO RAM-size summary remains 78,400 bytes; actual struct sizes and measured heap deltas above are the relevant separate memory observations. The 10-byte state plus 52-byte counters account for the 64-byte Engine growth; no large memory increase was found. Command remains 12 bytes, Pattern 3,106 bytes and project staging/encoded sizes remain unchanged.

Normal no-card firmware is restored on the board. Its analyzer confirms empty Chain/PATTERN defaults, zero Override/Fill/mix and counters, zero project fixtures/snapshot/apply/dirty state, zero PCM/cache/lock residue and no nonzero output. All normal heaps remain fixed.

Rejected/diagnostic captures are preserved separately: `GUITION_M18_SAMPLE_INITIAL_SERIAL.log` precedes the final coherent transport publication and was stopped before the UI summary, so it is not accepted qualification evidence. `GUITION_M18_SYNTH_BOOT_ONLY_SERIAL.log` contains only ROM boot output; Windows disconnected native USB with a ClearCommError. A fresh verified upload and complete capture passed. Neither diagnostic capture substitutes for the final logs. Manual physical touch/visual/listening checks remain unperformed; no claims about clicks or listening quality are made.

Final binary SHA-256:

- `guition_performance_stress`: `1d46128692059f7c5d726521568f4a56d09dee620e3d3edfbe1db420f9abfe8a`
- `guition_performance_synth_stress`: `0283b3edcb7eadf63a35cd50867af7fabeda7d090051d301f8616197c1a1281c`
- `guition_app`: `2554e4829f45f3a6d28c9f8060321eefdebb3fe116ebd1ff7cfa6f0cbc9ba0c7`

All **19 local PlatformIO environments** pass. All inherited 59 workflow host commands passed before the evidence follow-up; the complete extended **64-command workflow also passes** after the final captures and active-clock tests. Remote PR CI is reported separately after publication.

Commits: `9dc8f58` implements M18; the evidence follow-up records captures/report/README and adds active excursion sample-clock/RNG/event and exact two-entry golden comparisons. No firmware changes follow the accepted capture hashes.
