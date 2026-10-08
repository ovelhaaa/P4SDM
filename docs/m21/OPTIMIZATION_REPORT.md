# M21.1 — realtime DSP investigation (PARTIAL)

## Decision and exact baseline

**Linear cannot replace Nearest on this evidence. M21.1 is PARTIAL, not accepted.**
Nearest remains production interpolation 0. The PCM cache, block profiler and
32-bit Linear candidate are explicit diagnostic options, disabled in normal
builds. No M22, UI control, effect, resampler or project migration was added.

Local and remote main were both `7d90acea7aa18ddc7af1b8c5ad00d6b44cd4b16c`
(PR #13). The checkout was clean before investigation. The unmodified fallback
was `P4SDM_INTERPOLATION=0`; Project V2 was 52,704 bytes and Voice was 64 bytes.
All 86 inherited host commands passed before the cache change. A subsequent
87-command sweep including the cache comparison also passed; its transcript is
`docs/GUITION_M211_HOST_CHECKS.log`. Final targeted comparisons were repeated
after the 32-bit candidate and profiler were added.

The historical M21 results and all original logs are unchanged. In particular:

| Historical M21 | Maximum us | Headroom | Qualification limitation |
|---|---:|---:|---|
| Mixed Nearest | 4118 | 29.1% | inherited case only |
| Mixed Linear | 4426 | 23.8% | no Chain cycle |
| Mixed Hermite | 4963 | 14.5% | reserve and Chain |
| Full fractional Nearest, preparation excluded | 5643 | 2.8% | insufficient reserve |
| Full fractional Linear | unavailable | unavailable | watchdogs and Store fault |

Fresh captures use new `GUITION_M211_...` names. The locality diagnostic's
uncached full application run measured **5905 us and one deadline miss**;
its preparation-excluded maximum was 5781 us, render maximum 5640 us. This is
new rejected evidence, not a replacement for the historical baseline.

## Store fault: bounded analysis, no speculative fix

The original exception is in
`docs/GUITION_M21_LINEAR_WORST_FRACTIONAL_SERIAL.log`, lines 380–430:

```
MEPC 4001e07c  RA 4001df8e  SP 4ff794f0  GP 4ff12980
TP 4ff2cf50 T0 00000fff T1 666e6f63 T2 4455415b
S0 500d2000 S1 00000008 A0 4ff3de78 A1 4ff3de78
A2 00000000 A3 00000040 A4 00000009 A5 00000002
A6 00000043 A7 4ff3dde0 S2 4ff7b000 S3 4ff7b000
S4 00000000 S5 4ff78000 S6 4ff7b000 S7 4009c3ec
S8 00000001 S9 4ff7b000 S10 4ff7b000 S11 00000010
T3 206b6361 T4 6279616c T5 70203131 T6 33385345
MSTATUS 00001880 MTVEC 4ff00003 MCAUSE 00000007 MTVAL 500d2000
MHARTID 00000000
ELF SHA256 prefix b00e8a3f1
```

The complete original stack memory remains in that unedited log. No conventional
backtrace was printed, so a stack-word scan is not presented as a reliable unwind.
The Store fault occurs immediately after I2S initialization on a reboot, before
the fixture and its startup Voice benchmarks, unlike the sustained audio WDT.

The original matching ELF was not present. Rebuilding accepted main with the
same `guition_interp_linear_fractional_stress` environment produced SHA256
`2961122e19a6df569df255173f118b66d601fb103bc3804cd0b1da587619fbd7`.
It is preserved locally under `.pio/m211-baseline`, together with BIN/MAP.
Using the pinned `riscv32-esp-elf-addr2line -pfiaC` and objdump on that rebuilt
ELF gives:

```
4001e07c: sw a5,0(s0)
hw_cdc_isr_handler -> usb_serial_jtag_ll_write_txfifo
HWCDC.cpp:105, usb_serial_jtag_ll.h:142
4001df8e: hw_cdc_isr_handler, HWCDC.cpp:100
nm: 500d2000 A USB_SERIAL_JTAG
```

Both the recorded S0 and MTVAL equal the USB Serial/JTAG peripheral base, not
a PCM allocation address. This sharply bounds the likely failing operation to
a USB TX register store. **Because the ELF hashes differ, this remains a
provisional source attribution, not conclusive decoding of the original build.**
No PCM-pointer, out-of-region tap, Voice corruption, fixture lifetime, I2S buffer
ownership or stack-overflow diagnosis is established by this exception.
An ISR register-access/permission/reset-state problem is a remaining hypothesis;
no Arduino/IDF/platform change is made without a reproduction and matching ELF.
The current [Espressif errata](https://documentation.espressif.com/esp-chip-errata/en/latest/esp32p4/index.html)
were reviewed; they do not establish this exception's cause.

The reconstructed accepted firmware was flashed and captured independently in
`GUITION_M211_BASELINE_LINEAR_FRACTIONAL_SERIAL.log`. It repeatedly reproduces
the WDT with matching ELF prefix `2961122e1`, but did not reproduce a Store fault
during this capture. It is rejected, never counted as a successful safety run.

## Watchdog and ownership audit

Fresh accepted-firmware WDTs again name IDLE0 and CPU0 `app_audio`, around 15.46–
15.47 seconds after boot. Matching-ELF PCs include `40013426` (Linear arithmetic),
`4001358e` (mixing), `4001361e` (FX send), `40013840` (voice loop), and `4001397a`
(SimpleDelay). The changing PCs support ongoing rendering rather than one
stuck arithmetic instruction. They do not by themselves measure scheduler delay.

Audio remains on core 0 at `configMAX_PRIORITIES-1`; UI is core 0 priority 2.
The HAL downmixes into a fixed buffer and uses blocking `i2s_channel_write`.
Repeated blocks with little or no I2S blocking can starve IDLE0 while DMA
backlog is being consumed. The observed near-deadline uncached workload and
clean cached runs support that mechanism, but independent idle-task residency,
DMA callbacks/backlog and wakeup-delay measurements remain missing. No priority,
affinity, arbitrary delay, watchdog timeout/feed or watchdog-disable change was
made.

The fractional preparation loop executes inside `audio_worker`, after command
handling and before `render_buffer`. It does not mutate Voice from UI/storage.
`fixture_samples` has static lifetime, with resident PCM allocated before audio
startup. Storage retirement still uses the accepted Transfer acknowledgement;
voices stop before old PCM is freed. The diagnostic nonetheless rewrites musical
inputs and calls `set_increment` on active audio-owned voices each block. That
cost and its departure from immutable accepted-event-only configuration are
explicit limitations. It was kept identical for the isolated cache comparisons;
it has not been certified as a final replacement qualification fixture.

## Measured dominant cost: resident PCM access order

An allocation-free startup diagnostic reads actual resident PSRAM, 4096 reads
per block for 128 blocks per case. A timer is read only at block boundaries;
startup yields occur only between measurements. Volatile PCM reads prevent
folding. The 16 buffers are 131,072 bytes apart, as in the accepted fixture.

| Case | Mean us | Max us |
|---|---:|---:|
| Interleaved aligned independent buffers, sequential | 2333 | 2397 |
| One shared buffer, interleaved | 217 | 250 |
| Independent buffers, reverse | 2309 | 2375 |
| Independent buffers, stride 16 | 2327 | 2394 |
| Independent buffers, staggered offsets | 288 | 304 |
| Voice-major sequential reads | 153 | 185 |
| 32-frame gather then interleaved scratch reads | 267 | 301 |

Sequential, reverse, voice-major and gathered independent-buffer cases have the
same checksum (`4294942016`). Shared/strided/staggered cases have different
inputs and are explicitly controls, not musical equivalents. The order-only
comparison establishes a large memory-access/locality cost on the physical
P4; this is not a host cache inference. A cache-set-conflict explanation is
consistent with aligned-versus-staggered results, but hardware cache-miss and
PSRAM-bandwidth counters were not collected. Arithmetic alone cannot explain
the order-only difference.

## Individually measured candidates

1. **Bounded PCM read cache.** Each audio track owns 32 PCM16 entries. A miss
   gathers a contiguous bounded physical region; hits use internal scratch.
   This keeps the sample-major engine, trigger/choke/Repeat order, filters,
   gain, pan and Delay sends unchanged. Cache metadata checks PCM identity and
   slice bounds, and all entries are invalidated at each render block after
   ownership transfers. There is no cached sample ownership, allocation or
   storage access. All reads remain inside the current slice, including
   reverse and 1–4-frame slices. Device scratch plus metadata is **1344 bytes**
   (`nm` reports `pcm_read_cache` size `0x540`), outside task stack. Voice stays
   64 bytes. Retained as an opt-in candidate, not enabled in production.

2. **Exact 32-bit Linear numerator.** Keep the original int64 kernel unchanged
   as reference. The candidate computes `x0*(65536-f)+x1*f` in signed int32.
   Each product and their convex sum lie in [-2147483648,2147418112]. Quotient
   plus signed remainder implements ties-away rounding without overflowing by
   adding 32768 at a rail. Convex interpolation cannot exceed PCM16, including
   after rounding. No precision or saturation behavior is lost. All integer
   phases still bypass interpolation exactly. Retained as opt-in only.

   On one fresh run, the 65,536-iteration arithmetic controls measured 10107 us
   original fixed, 11215 us float, 9871 us int32, with matching fixed/int32
   checksum 3394. The complete workload improved less than arithmetic alone
   would justify promising; see the actual maxima below.

No scheduler redesign, voice-count reduction, short-region substitution,
per-frame timer, large permanent PSRAM buffer or Hermite algorithm change was
attempted. No speculative Store-fault fix was retained.

## 64-bit code generation and exactness

Disassembly of the accepted Linear lookup shows low/high `mul`/`mulh`, carry
handling, and a call to `quantize_q16` with register spills. Constant signed
division by 65536 is lowered to shifts/sign adjustment; it is **not** a general
64-bit divide there. Trigger/remaining calculations still contain `__udivdi3`,
including the diagnostic `set_increment` work. Dynamic fade division remains
32-bit. Neither position nor increment was narrowed: with any uint32 frame
limit the Q16 region is below 2^48, and accepted MIDI progression remains safe
in uint64. No overflow is used as an optimization.

The compiled nearest application render has no Linear/Hermite/quantize helper
calls; default compile-time arguments eliminate lookup-mode alternatives.
Startup qualification intentionally retains all modes. Additional indiscriminate
inlining was not used; M21's rejected inlining experiments remain evidence.

`pcm_cache_checks.py` regenerates untouched M21 Git headers in a distinct
namespace. It compares all 65,536 phases, all modes, exact-size short regions,
cache boundaries, reverse, all MIDI, fades/release, duration and replacement.
The int32 kernel also sweeps all phases for rail/zero/sign combinations.
Allocation guards pass. `interpolation_checks.py` independently retains the
pre-M21 exact Nearest comparison and existing retrigger/Gate/choke/retirement
tests. Historical source assertions normalize only the explicit cache branch
and two signature/lookup substitutions; the whole remaining Voice source and
Engine/event/RNG/Repeat/Project comparisons remain exact. Linux CI runs ASan
and UBSan, including the candidate.

## Block profiler and overhead limits

`P4SDM_BLOCK_PROFILE=1` adds five fixed 100-us-bin histograms and totals/maxima:
control/preparation, render, post-render bookkeeping, HAL write, and elapsed
block including I2S wait. It adds three boundary timer reads per block; no
audio-side Serial or allocation. Quantiles are bin upper bounds, not precise
sample percentiles. Static overhead is **2128 bytes**. The task stack high-water
measurement is taken once at the end; raw API result was **3240**.

| Profile stage | Mean us | p95 upper us | p99 upper us | Max us |
|---|---:|---:|---:|---:|
| Control/preparation | 161 | 299 | 399 | 505 |
| Render, all DSP | 3113 | 3799 | 3899 | 4000 |
| Bookkeeping | 70 | 199 | 199 | 141 |
| HAL write, including I2S wait | 2446 | 3699 | 3999 | 4069 |
| Complete elapsed block | 5791 | 5899 | 5899 | 5885 |

Render accounts for about 53.8% of elapsed mean and 93.1% of processing mean
excluding the HAL wait. These are aggregate stages; separate sequencer/SAMPLE/
SYNTH/filter/mix/Delay stage shares were **not** measured. HAL downmix/preparation
and I2S blocking were not separately timed. Complete elapsed time includes
intentional DMA pacing and is not substituted for the DSP deadline measurement.
Its maxima also do not establish an underrun or actual wakeup delay.

The comparable non-profiled cached Nearest run had processing p50/p95/p99/max
3615/3986/4090/4283 us. The profiling run had 3589/3978/4086/4417 us. Observed
max difference is +134 us, while lower percentiles decreased slightly. The
wall-clock UI fixture produces different event/Chain coverage, so this is **not
a controlled profiler-overhead bound** and nothing is subtracted. Final timing
judgements use non-profiled builds. Event-equivalent replay, independent wakeup/
idle residency and maximum DMA backlog remain qualification gaps.

## Fresh complete application measurements

Every clean candidate capture contains 13,782 blocks, actual PCM output, all
16 voices, inherited locks/filters/Delay/UI/transient/x8 workload and stable
heaps. The required processing maximum is **4643.991 us** (deadline 5804.989 us).
Preparation-excluded maxima are supplemental only; the table uses total
processing maximum. Exact machine-readable results are generated by
`tests/analyze_m211.py` into `optimization_device_results.json`.

| New candidate | p50 | p95 | p99 | Max us | Headroom | Chain loops |
|---|---:|---:|---:|---:|---:|---:|
| Nearest + cache, B | 3615 | 3986 | 4090 | 4283 | 26.2% | 0 |
| Linear + cache, B | 4441 | 4784 | 4858 | 5058 | 12.9% | 0 |
| Linear32 + cache, B | 4226 | 4583 | 4674 | 4927 | 15.1% | 1 |
| Nearest + cache + profiler, B | 3589 | 3978 | 4086 | 4417 | 23.9% | 0 |
| Nearest + cache, A | 3422 | 3837 | 3943 | 4279 | 26.3% | 0 |

Nearest cache B has 6339 all-sixteen-fractional blocks; its x8 capture has 1066
full blocks. The profile run has 6422 all-sixteen-fractional blocks. Zero
watchdogs, panics, processing misses, I2S failures and timeouts were recorded
in these completed candidate captures. The duration covers the previously
reproduced WDT interval, but extended sustained qualification/reset counters
are not complete. Different parent/event/RNG/Chain counts prevent treating
these runs as fully equivalent musical executions. In particular, visiting
all Chain entries is not equivalent to completing a Chain loop.

Scenario A is captured separately and fails the unchanged strict M20 analyzer
because its Chain loop count is zero. Its maximum is 161 us higher than the
historical M21 mixed Nearest maximum; event coverage differs, so neither a
controlled regression bound nor a mixed-workload speedup is claimed.
B labels here denote the **retained M21 full-region-intent fixture**. Its
configuration and slice counters report full regions, but command auditions
can trigger before preparation and active Voice regions are not independently
counted at every frame. Consequently these are B attempts, not certification
that every active voice had a full region throughout the entire capture.
Scenario C (varied pitch/phase, reverse, sample lengths/regions) has
host and isolated Voice/locality coverage, **not** a completed application
qualification capture. It was not substituted for A or B. Fresh Hermite and
SYNTH candidate application qualifications were not completed; their accepted
controls and all inherited host checks remain in CI.

## Memory, SD, CI and outstanding acceptance

Cached B PSRAM free/largest remain 24265252/24117236 before and after; internal
free remains 216476. Profile internal free is 214348 before and after. Cache
and profile deltas are exactly 1344 and 2128 bytes relative to the matching
uncached diagnostic, with zero new PSRAM allocations. No Voice, Track, event,
Project or UI layout change occurs. Project V2 is still byte-identical,
52,704 bytes; Engine/event/RNG/Repeat comparisons cover 7,200,000 samples.

The physical SD mounted successfully in every fresh application capture; its
files were indexed. This is **not** a fresh M21.1 WAV replacement/Save/Load/
SliceLock/choke qualification. M21's actual physical SD workflows remain
historical evidence and were not relabeled as M21.1. No user project was
overwritten. UI layout/render/allocation checks remain inherited and pass.

The complete existing workflow is retained, with candidate tests/builds and
matching ELF/BIN uploads added. A new clean-capture validator rejects candidate
watchdogs, panics, misses, I2S faults, incomplete summaries and heap drift.
It reports timing/Chain rejection separately; `--require-acceptance` fails
explicitly while required evidence is missing. Green engineering CI cannot
promote this PARTIAL milestone. Final CI status is available on
[draft PR #14](https://github.com/ovelhaaa/P4SDM/pull/14/checks) and reported at
delivery. Candidate and baseline build hashes are recorded in `optimization_builds.json`.
PlatformIO invalidated the first clean cache-candidate build directories when
the environment list changed. Their previously recorded SHA values are retained,
but matching local ELF availability is explicitly false in the manifest. The
failing baseline, profiling build and later final candidates were copied outside
the build directory. CI archives its own newly built ELF/BIN files; these are
not falsely identified as the Windows physical-capture ELFs. New serial logs
are stored with Git text conversion disabled, preserving their raw bytes.

The measured initial bottleneck is interleaved aligned PCM access. After the
cache, Linear still exceeds the reserve by **283.009 us** in its best measured
complete non-profiled B candidate. Which post-cache DSP substage dominates
that remaining maximum is unresolved; the aggregate render histogram is not
an exact filter/Delay/arithmetic attribution.

Remaining M21.1 work is narrowly bounded: recover/reproduce the USB Store fault
with a matching ELF; configure fractional workloads entirely through accepted
event inputs; replay matched A/B/C controls with full Chain/Repeat coverage;
profile DSP substages and DMA scheduler/backlog independently; then repeat
extended SAMPLE/SYNTH and physical SD regressions. Nearest remains the safest
known production behavior. Neither the opt-in Nearest cache nor Linear is
universally qualified by this partial evidence.
