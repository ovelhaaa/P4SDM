# M19P — physical SD qualification

Started 2026-10-07, America/Sao_Paulo, Guition JC4880P443 / ESP32-P4 revision
1.3, COM13. Starting branch `codex/m19-transient-slicing`, head
`dd54fa9c42ba94ac65f160826ec66f9ee7e5af3d`. User authorized resuming COM13
after card preparation. This session did not format the card, delete unrelated
contents, merge a PR, or start M20.

PR [#9](https://github.com/ovelhaaa/P4SDM/pull/9) was already merged externally
at 2026-10-07 21:03:06 UTC (18:03:06 local), commit
`e8e1e2e0633d9e03165297fc69b0aae1058c3a95`. This is a corrective follow-up,
not pre-merge evidence for #9.
The shared checkout had advanced to `main` at that merge (identical source tree
to the starting M19 head). Follow-up work is on `codex/m19-physical-sd`.

**Synthetic physical SD qualification: passed the checks described below.**
**REAL MUSICAL LISTENING PENDING.**
**PHYSICAL TRANSIENT AUTO-SLICING VALIDATION PENDING.**
Electrical power-cycle and abrupt power-loss qualification remain pending.
The genuine hardware resets below do not constitute removal of board power.

## Card and WAV inventory

Original normal firmware was flashed/hash-verified and freshly booted:
[`NORMAL_SERIAL`](GUITION_M19_PHYSICAL_SD_NORMAL_SERIAL.log).
SD mounted in 4-bit mode at 20 MHz, capacity 8,053,063,680 bytes; fresh mounts
about 245 ms. Storage inspection reported FAT volume total 8,050,966,528 bytes
and accessible `/P4SDM/SAMPLES` and `/P4SDM/PROJECTS`. FAT32 was reported by the
separate preceding card-preparation session; this qualification did not probe
the FAT subtype independently. HAL `format_if_mount_failed=false` is unchanged.

Browser initially found zero WAVs. Temporary helper generated deterministic
fixtures under reserved `m19p_` names, preserving any existing same-name file.
These are not musical recordings. WAV bytes remain only on SD, not in Git.
Inventory: [`SEED_SERIAL`](GUITION_M19_PHYSICAL_SD_SEED_SERIAL.log),
[`FINISHSEED_SERIAL`](GUITION_M19_PHYSICAL_SD_FINISHSEED_SERIAL.log), final
[`FIXED_INTEGRATED_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_INTEGRATED_SERIAL.log).

Nine indexed files: seven accepted, two explicitly rejected. Valid inputs are
PCM16/44.1 kHz. Stereo retains the accepted resident mono downmix.

| Filename | Frames / channels | File bytes | Result / waveform FNV hash |
|---|---:|---:|---|
| `m19p_impulses.wav` | 176400 / 1 | 352844 | loaded / `c78d1545` |
| `m19p_kick.wav` | 176400 / 1 | 352844 | loaded / `af245265` |
| `m19p_snare.wav` | 176400 / 1 | 352844 | loaded / `5e370bb2` |
| `m19p_mixed.wav` | 176400 / 1 | 352844 | loaded / `0720701a` |
| `m19p_stereo.wav` | 176400 / 2 | 705644 | loaded, 352800-byte resident mono / `595ef306` |
| `m19p_large.wav` | 2097152 / 1 | 4194348 | loaded, 4194304-byte resident limit / `0e481651` |
| `m19p_oneshot.wav` | 22050 / 1 | 44144 | loaded / `0ee7dbac` |
| `m19p_bad_depth.wav` | 24-bit header | 352844 | rejected: unsupported depth (16-bit only) |
| `m19p_truncated.wav` | malformed RIFF | 352841 | rejected: invalid RIFF length |

[`LOADS_SERIAL`](GUITION_M19_PHYSICAL_SD_LOADS_SERIAL.log) and post-fix
[`FIXED_LOADS_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_LOADS_SERIAL.log): accepted
loads publish matching names, distinct waveforms and nonzero rendered PCM.
Rejected loads preserve the preceding large sample/hash. Baseline durations:
mono 1.07–1.15 s, stereo 1.675 s, large 8.235 s, one-shot 0.518 s.
Retired PCM is reclaimed on replacement. Hashes establish waveform regeneration,
not visual appearance. Audition output was measured, not listened to.

## Slice and transient checks

[`SLICING_SERIAL`](GUITION_M19_PHYSICAL_SD_SLICING_SERIAL.log): SAMPLE pitch 67
→ SYNTH 43 → SAMPLE 67 retains filename/independent pitches; equal 4/8/16
divisions, slice selection/audition, manual start 9000/end 19000, enabled
eight-slice APPLY. Host asserted CANCEL leaves every reported bank boundary
identical. Manual overlap follows existing behavior.

Six fixtures × LOW/MED/HIGH × NATURAL/8/16 = 54 completed proposal checks:

| Fixture | LOW natural/8/16 | MED | HIGH |
|---|---|---|---|
| impulses, kick, snare, mixed, stereo | 16 / 8 / 16 each | 16 / 8 / 16 each | 16 / 8 / 16 each |
| one-shot | 1 / 1 / 1 | 1 / 1 / 1 | 1 / 1 / 1 |

No musical MED verdict or detector tuning follows from these synthetic attacks.
[`STALE_SERIAL`](GUITION_M19_PHYSICAL_SD_STALE_SERIAL.log): trim during large
analysis discards proposal; different-sample replacement discards another
(`stale_discarded=2`). [`PLAYBACK_SERIAL`](GUITION_M19_PHYSICAL_SD_PLAYBACK_SERIAL.log)
records 9 triggers, 2 reverse, 4 Gate, 5 releases, 6 choke scans, 3 retriggers,
nonzero PCM and zero render errors. Slice Locks are also restored below.
Scripted commands do not establish physical gestures or pad-hold behavior.

## Concrete defect and production fix

Saved-project activation with two WAV tracks overflowed the 7000-byte storage
task stack. Independent console-only proof retained byte-identical original
`samples.cpp`/`samples.h`, excluding fixture/storage helpers, and reproduced
the failure. [`PROJECT_RESTORE_BEFORE_FIX_SERIAL`](GUITION_M19_PHYSICAL_SD_PROJECT_RESTORE_BEFORE_FIX_SERIAL.log):
panic SP `4ff2ccc0` below lower bound `4ff2cd8c`; PC `40053772` decodes to
`_svfprintf_r`, through nested project/WAV restoration.
[`STACK`](GUITION_M19_PHYSICAL_SD_STACK.log) contains disassembly/source/image hashes.

Only production change: the existing 4096-byte WAV read buffer becomes aligned
static BSS instead of local stack. Existing single storage owner makes it safe
to share across serial loads. Internal BSS +4096 bytes. Stack size, Project V2
format, WAV contract, detector and renderer are unchanged.

## Project and Chain persistence

[`SAVE_SERIAL`](GUITION_M19_PHYSICAL_SD_SAVE_SERIAL.log): after correcting host
step-programming setup, complete 52704-byte V2 slots A/gen3/BPM137/CRC3850630c
and B/gen4/BPM143/CRCae47d8f2 both validate. Earlier generations1/2 were setup
attempts, not the populated-pattern qualification.

Last session of [`FIXED_RESTORE_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_RESTORE_SERIAL.log)
uses esptool reset and requires fresh PSRAM/I2S setup, SD mount and indexing.
Earlier bare-RTS attempts were not accepted as reset evidence. LOAD restored:

- BPM143, swing63, Chain mode; stopped, transient solos cleared. Delay was
  enabled in setup; its global persisted flag was not separately printed.
- Track0 mixed PCM/pitch64/synth36, region1024..62000, Gate/choke1,
  cutoff48/resonance37/send91, enabled eight-slice bank; slice1=9400..19000.
- Track1 snare PCM/pitch58/synth39, OneShot/choke1/cutoff72/send44.
- Track2 synth51; Track3 muted; both resident sample names/frames/hashes match.
- Four 16-step patterns, masks1111 on three programmed tracks, Slice Locks
  0/1/2/3 on every pattern. Chain P01×2/P02×1/P03×3/P04×1, Loop ON.

Sixty transport snapshots and actual `engine.chain_loops=2` verify two complete
loops and entry/repeat progression. Old helper counters mislabelled as Chain
are not used for this assertion.

Last [`FIXED_MISSING_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_MISSING_SERIAL.log)
session: snare renamed, fresh reset/load gives `PROJECT READY / 1 MISSING`;
metadata usable, missing SAMPLE track has zero frames. Cold isolated trigger
produces zero nonzero PCM samples. Unchanged project retains filename reference.
Rename back + reset/load restores snare/hash and clears MISSING. Prior warm
attempt included DSP tail and is not counted as silence evidence.

Last [`FIXED_FALLBACK_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_FALLBACK_SERIAL.log)
session: backup newest B, flip payload byte40, B rejects CRC, A remains valid;
fresh reset/load restores BPM137. Restore byte-exact backup, CRC/BPM143 return.
This is controlled file-content corruption, not filesystem or power-loss testing.

[`FIXED_CYCLES_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_CYCLES_SERIAL.log): 12
load/replace/analyze/APPLY cycles, identical internal free218248/PSRAM30032420/
largest29360116 at settled states. Four additional cycles including SAVE/LOAD
have identical free heaps; largest alternates29360116/29884404 and returns to
the larger value. Final A/gen7 and B/gen8: V2, 52704 bytes, CRC2c4d1d2b, valid.
Recursive final inventory contains nine fixtures and two project slots, no
renamed sample/QBACKUP/zero-length regular file. Fixtures/project remain on SD.

## Render metrics and regression evidence

Temporary console/fixture/file inspection run on existing UI/storage owners in
an ignored firmware copy. Production builds include none of them. Tools:
[`tests/physical_sd`](../tests/physical_sd/README.md). Instrumentation adds
overhead: stored p99, all-window max/errors, active PCM voices/output. Overlap
samples `transients::active` after I2S write; final completion can fall outside
overlap, but overall max covers it. Render times are not SD load/UI latency.
Deadline5804.989 µs; ≥20% headroom requires max≤4643.991 µs.

All rows below have zero deadline misses, I2S failures and timeouts:

| Window | Blocks | p99 / max µs | Overlap blocks / max µs | Nonzero PCM samples |
|---|---:|---:|---:|---:|
| post-fix WAV loads/rejects | 6182 | 1978 / 2065 | 0 / 0 | 3154560 |
| reset/restore + Chain | 5793 | 2192 / 2258 | 0 / 0 | 2931972 |
| 12 load/analyze/APPLY cycles | 14494 | 1798 / 1894 | 116 / 1842 | 0 (stopped) |
| 4 cycles including SAVE/LOAD | 9999 | 1842 / 1924 | 38 / 1814 | 0 (stopped) |
| dense 16 PCM tracks + x8 + analysis/load/APPLY | 5280 | 3838 / 3934 | 1970 / 3934 | 2703360 |

[`FIXED_DENSE_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_DENSE_SERIAL.log): 240BPM,
16 steps ×16 sample tracks, velocity127/probability100/ratchet4, eight locks,
delay, original waveform/AutoSlice page, x8 repeat/capture_count16. Track0 used
4MiB file; six MED/16 analyses canceled, mixed-file replacement analyzed/applied
while playing. Active PCM voices16, **32.23% worst render headroom**, repeat
accepted1/hits51952. Offline analysis maximum1861512 µs, elapsed chunk45164 µs
(includes scheduling), separate from render3934 µs. Touch latency unmeasured.

All **75** existing workflow host/analyzer commands pass after fix, including
M16/M17/M18.1 and unchanged analyzers of retained M19 captures:
[`CI_HOST`](GUITION_M19_PHYSICAL_SD_CI_HOST.log).
All **23** firmware environments in the existing workflow pass:
[`CI_FIRMWARE`](GUITION_M19_PHYSICAL_SD_CI_FIRMWARE.log). First six ran
individually; the incomplete seventh was restarted, then the remaining 17 ran
in one invocation. `TOTAL=23 FAILURES=0`. The supplementary physical capture
checker also passes (`tests/physical_sd/analyze.py`). Fresh production captures
are recorded below.
The workflow adds this physical-capture check after all existing commands;
the original 75 host/analyzer checks and 23 firmware environments are retained.
No inherited workload, assertion, target or compiler flag was weakened.

Original fresh normal analyzer failed exact internal heap equality:
295880→296408 (+528), PSRAM unchanged. This gain is not a leak but remains a
failed assertion; later stable cycles do not retroactively change it. No old
 no-card capture substitutes for new evidence. Initial helper/empty-index
attempts and retried resets are preserved as setup/failed evidence.

Fresh unchanged production sample stress, with card mounted and nine indexed
WAVs: [`FIXED_STRESS_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_STRESS_SERIAL.log).
13782 blocks, p99=3465/max=4181 µs, zero misses/I2S errors/timeouts, active_max16;
analysis overlap317/x8_blocks317/worst4181, qualification chunk4410 µs,
storage stack free minimum4596. Render headroom27.98%. The unchanged inherited
analyzer fails startup internal equality239068→239596 (+528), PSRAM/largest
unchanged. Therefore this fresh capture is **not an end-to-end analyzer PASS**.
A transient 512-byte SD-probe DMA allocation plus allocator overhead is a
possible explanation, not a proved cause. No startup snapshot/assertion was
changed to suppress it. The full workflow passes its retained original captures;
that does not retroactively turn this new-card capture into a pass.

Fresh production synth stress:
[`FIXED_SYNTH_STRESS_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_SYNTH_STRESS_SERIAL.log),
13782 blocks, p99=4199/max=4425 µs, zero misses/I2S failures/timeouts,
active_max16, **23.77% render headroom**. Analysis overlap202/worst3200/x8=202;
qualification chunk3824 µs. Internal/PSRAM/largest heaps exactly unchanged.
Its inherited M5→M12 analyzers pass, but M13 then explicitly requires
`'NO CARD' in data`; this mounted-card capture correctly reports SD READY and
therefore fails that no-card-specific assertion. Neither that assertion nor
the stress firmware was modified. These fresh mounted-card runs supplement
the original no-card qualification rather than claiming its full analyzer PASS.

Board finally restored to production `guition_app`, 720192-byte image,
SHA256 `0dd166bf5ea9da5370c86f8b5c4705af9ec38832ea4fff6438c2eac87ed883a3`.
[`FIXED_NORMAL_SERIAL`](GUITION_M19_PHYSICAL_SD_FIXED_NORMAL_SERIAL.log): fresh
SD mount/index9, 10337 blocks, p99=1767/max=1918 µs, zero misses/I2S errors/
timeouts, transport/output inactive. Heap291676→292204 (+528), PSRAM unchanged;
same exact-equality analyzer failure. Full unchanged fresh analyzer outputs and
exit codes: [`FRESH_ANALYZERS`](GUITION_M19_PHYSICAL_SD_FRESH_ANALYZERS.log).
The qualification console is no longer installed. Both valid project slots and
the nine fixture WAVs remain available for operator tests via the normal UI.

## Explicit acceptance answers

1. **M19 safe to merge?** Requested physical/musical gate incomplete; #9 already
   merged externally. Concrete stack defect fixed/qualified for separate review;
   no merge by this session.
2. **Physical transient auto-slicing validated?** Synthetic SD analysis,
   CANCEL/APPLY/stale checks yes; real music/touch/listening pending.
3. **Project V2 save/power-cycle/load physically validated?** Real SD SAVE →
   hardware reset → LOAD/sample restoration yes. Electrical power removal no;
   retain broader power-cycle gate.
4. **Chain V2 persistence physically validated?** Yes for synthetic SD project
   across hardware reset, exact entries and two loops. Physical V1→V2 migration
   and electrical power-cycle untested.
5. **MED acceptable default on real musical material?** Undetermined; no
   drum-break/percussion/vocal recordings or listening.
6. **Clicks/dropouts/deadline misses?** Final measured SD windows have zero
   deadline/I2S errors; audible clicks/dropouts cannot be assessed without listening.
7. **Remaining physical validation?** Real musical LOW/MED/HIGH listening,
   touchscreen gestures/pad hold/manual slicing, visible waveform/proposals,
   audible reverse/Gate/choke/retrigger/Repeat, electrical power-cycle, physical
   V1 migration, card removal/reinsert, abrupt power loss during SAVE.
   The +528-byte startup heap snapshot discrepancy remains unproved; a global
   Delay toggle/save/reset/restore observation was not separately recorded.
