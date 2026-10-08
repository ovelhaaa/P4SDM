# M21 — interpolation implementation and qualification

Accepted parent: `4ac1015d0254a84c5e508317f2bea077257e0fe2` (M20.1,
merged PR #12). Its accepted head `b0083a9` remote workflow
[37822533481](https://github.com/ovelhaaa/P4SDM/actions/runs/37822533481)
was independently confirmed completed/success before implementation.

## Exact accepted playback audit

`wav.h::Voice` owns `uint64_t position/increment`. Trigger resolves normalized
START/END by `uint64_t(normalized)*frames/65535`, clamps start to frames-1,
repairs inverted/empty regions to one frame and starts logical position zero.
No fractional physical region start exists. The semitone table is rounded Q16,
octaves are exact shifts, MIDI is clamped 0–127, C4/60 is 65536. That table and
`sample_playback.h` are unchanged.

Old next() returns zero for inactive/null Sample and terminates out-of-region
or exhausted transport. It reads exactly data[start+floor(position/65536)]
forward or data[end-1-floor(position/65536)] reverse, discarding fractional
phase. Attack gain starts at 0, increases up to 32, and is limited by
remaining-1 at the end and release_left-1 during release. PCM times integer
gain divided by fade truncates toward zero. There is one such fade operation.
Remaining is ceil((region_length*65536-position)/increment). Every valid
output decrements it; the final output stops without advancing position.
Otherwise position advances by increment. Gate/choke release lasts the same
32 output frames (or remaining duration), and retrigger resets transport/fades.
No scheduler, envelope or extra latency is introduced by M21.

`app_main.cpp::trigger` receives accepted TriggerEvent Playback; sample
auditions, slices, locks, ratchets and Repeat all reach the same Voice renderer
through app_pcm(). Voice routing remembers the accepted SAMPLE/SYNTH source.
`voice_state.h` does not re-resolve the accepted slice after base edits.
`samples.cpp::load_name` allocates/stages PCM on storage, publishes Transfer,
waits for audio acknowledgement and frees retired PCM only after Voice.assign
has stopped the old voice. Project detachment similarly stops voices before
retirement. No PCM tap is cached beyond next(). These sources and musical
ownership/snapshots are unchanged except for compile-time diagnostics.

The untouched control is regenerated directly from accepted Git headers into
a temporary `accepted` namespace, rather than reimplementing old next(). The
inherited source-equivalence check reverses only the three exact M21 lookup
substitutions and still asserts the entire historical wav.h source.

## Reconstruction and representation

`sample_interpolation.h` has a bounded lookup API and Interpolation enum. It
knows only PCM, Region, reverse and Q16 position. Voice validates transport
before its trusted lookup; the standalone public lookup also rejects invalid
regions/coordinates without accessing PCM. The caller supplies a region inside
the actual allocation. No musical state is re-resolved.

Logical taps replicate the nearest edge: forward start+clamp(k,0,length-1),
reverse end-1-clamp(k,0,length-1). No wrapping across slices. One-, two-, three-
and four-frame regions work with repeated taps. Fraction-zero returns exact
x0 for all modes, before any polynomial work. Pitch, remaining, frame_index,
Gate, release/choke, fade gain and position advancement are unchanged.

* Nearest: y=x0, exactly the accepted control.
* Linear: y=x0+t*(x1-x0), t=fraction/65536. Selected implementation uses an
  int64 Q16 accumulator, one final nearest rounding (ties away from zero) and
  PCM16 saturation. The largest products fit well inside int64. Float candidate
  retained for diagnostic timing/precision comparison.
* Hermite4: Catmull–Rom tangents m0=(x1-xm1)/2, m1=(x2-x0)/2;
  a=(-xm1+3*x0-3*x1+x2)/2, b=(2*xm1-5*x0+4*x1-x2)/2,
  c=(x1-xm1)/2, d=x0; y=((a*t+b)*t+c)*t+d. Selected implementation uses
  bounded single-precision float, final nearest rounding (ties away from zero)
  and saturation to [-32768,32767]. No NaN/Inf can arise from these bounded
  PCM16 operands. Independent double Hermite-basis comparison bounds the
  rounded output difference to one PCM unit across the edge/extrema sweeps.
  Fixed candidate retains Q16 through Horner stages; intermediate truncation
  also stays within one PCM unit in the tested sweep.

The signal order is interpolation → the single inherited fade gain → existing
velocity/filter/pan/Delay processing. The UI and WAV formats are unchanged.
All choices are stable compile-time modes; there is no CPU-driven switching.

## Objective evidence and limits

[QUALITY.md](QUALITY.md) and [quality.csv](quality.csv) contain 357 comparisons
against a normalized 384-tap Blackman-windowed sinc with pitch-dependent cutoff
min(1,1/ratio), using actual C++ lookup outputs. Fixtures include DC, impulse,
step, ramp, sine, chirp, noise, clipped burst, smooth transient and alternating
full scale. All requested frequencies and seven pitch ratios are retained,
including difficult and above-Nyquist cases. RMS/peak error, amplitude error,
spurious energy and folded alias-tone RMS are reported. Reference convergence
against 768 taps is also recorded for every sine/pitch combination. Sine harmonic energy
(orders 2–5 below Nyquist) and residual nonharmonic energy are separated by
least-squares projection; this is finite-window energy estimation, not a claim
of a complete perceptual metric or ideal resampling reference.

At 1 kHz/octave down, RMS error is approximately 570.6 PCM units Nearest,
20.1 Linear, 0.2 Hermite. At 18 kHz/fifth up, Hermite has *more* error/alias
energy than Linear: roughly 8567.7 versus 7032.0. Neither interpolator supplies
the antialias filtering needed when pitching upward. Octave-up integer phases
give identical outputs in all modes, including the same aliasing. Unity output
is exactly the original PCM, not merely similar.

Full-scale Catmull–Rom plateaus overshoot to +40958.875/-40959.875 PCM units.
65535 of 65536 phases exceed the rail; clamp-induced RMS difference from the
unclamped polynomial is 5982.5 units. Alternating full scale and the central
step interval are also reported. Saturation never wraps. No bounded cubic,
resampling filter, oversampling or additional envelope is silently introduced.

Generated matched-control WAVs and longer RMS-matched musical proxies are
prepared under ignored `.pio/m21-quality`. They contain no copyrighted material.
Preparation and synthetic measurements do not establish actual listening,
real vocal/instrument performance, physical touch/display appearance or
subjective preference. Those remain distinct acceptance evidence.

## Safety and integration

All 65536 phases across all short regions and directions are checked against
independent double equations; exact-size PCM allocations enable Linux ASan to
catch even zero-weight out-of-region taps. All 128 MIDI pitches, inverted
stored regions, extrema, retrigger, fades, Gate/choke, end duration and PCM
replacement are covered. Real new and accepted Voice are compared sample for
sample in Nearest. All modes have matching unity PCM and transport duration.
The allocation guard covers the playback tests, including Transfer retirement.
Linux CI also runs AddressSanitizer and UndefinedBehaviorSanitizer.

Inherited event/RNG/Repeat/codec simulations compare 7,200,000 samples against
actual Git sources and preserve encoded Project V2 bytes: **52,704**.
`Playback`, `TriggerEvent`, project and Track layouts are unchanged.
Voice is **64 bytes before and after** on host and device. No interpolation
table, coefficient buffer, scratch buffer or persistent voice state is added.
Ordinary interpolation performs no allocation, I/O, logging, locking or wait.
The diagnostic startup benchmark yields only between measured blocks.

## Qualification process

All three stress environments extend the complete guition_ui20_stress chain:
16 SAMPLE voices, 240 BPM, x4 ratchets, all locks, filters, Delay, Chain,
Fill/Override, x8 Repeat, concurrent transient analysis and unchanged display/UI.
`P4SDM_INTERPOLATION=0/1/2` selects Nearest/Linear/Hermite respectively;
`P4SDM_INTERPOLATION_QUALIFICATION=1` adds isolated pre-audio benchmarks and
block-level fractional-work counters. The latter counts blocks beginning with
fractional increments and separately those beginning with all 16 voices active;
it does not assert all sixteen increments are fractional. The isolated
fractional scenario does run all sixteen at MIDI 59. Other scenarios cover
unity, mixed pitches, reverse, 1–4-frame slices and Gate releases.

Raw Voice benchmarks include output checksum and fade/transport cost, omit the
rest of the audio engine, and are not substituted for full-block acceptance.
Arithmetic candidate timings vary both PSRAM taps and phase; an earlier
constant plateau could be compiler-optimized and is not the decision evidence.
Firmware/compiler remains the pinned accepted pioarduino 55.03.36-1/360 MHz,
44.1 kHz, 256 frames. Deadline 5804.989 us, selected-default limit 4643.991 us.

The first diagnostic startup did not yield and tripped the idle watchdog. Its
unaltered capture is retained as failure evidence. The final diagnostic yields
outside measured blocks. An initial Nearest capture ended during its summary
and measured 4666 us (19.6% headroom), so it is not accepted. Removing redundant
per-frame region checks and ensuring hot-path inlining preserved documented
semantics; the new Nearest capture measured 3947 us and passed inherited checks.

Device results, SD evidence, final CI, and the default decision are recorded
below after final captures. A milestone is not accepted solely from host tests.

## Final device evidence and decision

The selected fallback remains **Nearest (0)**. M21 is **not accepted as a
qualified production-quality upgrade**. The alternatives remain explicit
compile-time options for evaluation; there is no adaptive selector. Improved
objective reconstruction alone does not establish the complete realtime or
perceived-musical-quality acceptance case.

| Complete inherited mixed SAMPLE run | max us | headroom | deadline misses / failures / timeouts | Chain loop coverage |
|---|---:|---:|---|---|
| Nearest final | 4118 | 29.1% | 0 / 0 / 0 | present |
| Linear final | 4426 | 23.8% | 0 / 0 / 0 | absent |
| Hermite final | 4963 | 14.5% | 0 / 0 / 0 | absent |
| Unchanged SYNTH control | 4510 | 22.3% | 0 / 0 / 0 | present |

Every complete mode run captured 13782 blocks and 16 active voices. Nearest
and SYNTH pass the unchanged strict inherited M20 analyzer. The Linear run
passes the time budget but fails the inherited Chain-cycle coverage requirement;
Hermite fails the 20% headroom requirement. Neither is presented as fully
qualified. Per-mode machine-readable results are in `device_results.json`.
The three final captures are `docs/GUITION_M21_{NEAREST,LINEAR,HERMITE}_FINAL_SERIAL.log`.
The SYNTH capture is `docs/GUITION_M21_SYNTH_STRESS_SERIAL.log`.

Mixed fixture counters count any fractional voice, with a separate all-active
counter. Additional diagnostic environments force all 16 increments through
unchanged MIDI 59, with full regions and forward playback, preserving the
original three fixtures separately. They time input preparation, render only,
and full-block time minus preparation independently, per block. They modify
benchmark musical inputs before rendering and force active increments; they
are not evidence of immutable event ownership (the separate equivalence and
SD tests cover that). All additional timing variables compile out in production.
The earlier all-fractional mixed-region Linear run measured **5136 us / 11.5%
headroom** over 5875 all-sixteen-fractional blocks, with no misses, but did not
separate preparation overhead; it is retained as rejected evidence. Final
full-region attempts and their completeness are recorded in `device_results.json`.
A capture that times out without a final report supports no inferred timing or
safety result. No serial fragments are filled in or repaired.

The complete full-region Nearest control measured 5732 us total, 5643 us
excluding each block's measured preparation, and 5514 us render-only. Even
the preparation-excluded equivalent has only **2.8% headroom**, below 20%,
with 7017 all-sixteen-fractional blocks, zero misses/failures/timeouts and a
covered Chain cycle. Thus the additional worst case also exceeds the reserve
with the original renderer. This does not invalidate its accepted historical
fixture; it exposes a previously unqualified input configuration. The matched
Linear full-region capture repeatedly triggered the IDLE0 watchdog while
`app_audio` was running and also recorded a Store access fault panic. It
rebooted before completing the capture. This is an explicit rejected failure,
not a safe run or a timing estimate. No improvement/default is qualified in
that case. The original unedited log preserves registers and backtraces.

PSRAM before/after is identical for every complete mode run: free 24265252,
largest block 24117236; internal free 217836. The diagnostic counters account
for 16 internal bytes relative to the accepted 217852; ordinary interpolation
adds zero persistent state. Extra preparation profiling adds a further 12
internal bytes only in the extra diagnostic environments. Voice remains 64
bytes, scratch/table allocation zero. SYNTH heaps are also unchanged across its
capture. No realtime allocation or filesystem call was introduced.
The extra Nearest capture measured internal free 217820 before and after
(32 bytes below the accepted baseline, including compiler alignment).

The arithmetic choice is fixed Q16/int64 Linear and float Horner Hermite.
Arithmetic candidates use varying PSRAM taps/phases; forcing arithmetic-helper
inlining improved raw tests but worsened the complete workload (4762 us Linear,
5080 us Hermite). That experiment was reverted. Only bounded tap lookup and
Voice hot-path inlining remain. Earlier failed and incomplete captures are
retained with descriptive filenames; acceptance uses the complete final files.

## Physical SD and UI

Both Linear and Hermite were flashed and exercised against the physical mounted
SD using the inherited qualification console. The final sessions covered
forward/reverse playback at pitches 48/59/61/67/72, slices and SliceLocks,
Gate/choke, active sample replacement, x8 Repeat while changing locks/slice/sample,
and saving/reloading `M21_INTERP_QUAL/A.P4P`. Project V2 stayed **52704 bytes**;
slot headers and CRC validation passed. The console works with actual synthetic
WAV files on SD (176400 frames), not a simulated filesystem. It is a temporary
qualification build, not a new production UI.

Linear: 8207 blocks, max 2235 us, 910 Repeat hits. Hermite: 8146 blocks,
max 2357 us, 910 Repeat hits. Both final sessions had zero deadline misses,
write failures and timeouts, exercised two simultaneous SD voices and reverse,
and reloaded valid V2 generations 1 and 2 respectively. These sparse SD timing
results do not replace the 16-voice dense stress. A sparse-step Repeat test and
an initial USB reenumeration failure are retained in their logs; the final
sessions are explicitly delimited. Only the reserved qualification project was
written; the existing M19P project was preserved.

Host rendering verified 19 UI pages, 494 widgets, 2904 hit edges, no overlap,
text fit/navigation and zero render allocations. Device captures include the
actual M20 page/update workload. No human touchscreen walkthrough or external
visual inspection is claimed. No human musical A/B listening was performed;
12 RMS-matched percussion/melodic proxy WAVs were prepared under ignored
`.pio/m21-quality` for audition. There are no invented listening observations.

## Reproduction, CI and remaining acceptance gap

Run `python tests/interpolation_checks.py`, install NumPy 2.3.5 and run
`python tests/interpolation_quality.py`. Linux additionally executes AddressSanitizer
and UndefinedBehaviorSanitizer. `python tests/analyze_m21.py` validates complete
records and records explicit alternate rejections. Strict `analyze_m20.py`
runs independently on fresh Nearest and SYNTH captures. Existing M3-M20.1
workflow checks remain unchanged. CI builds the original environments plus
three interpolation and two additional fractional environments. Physical SD
reproduction uses `interpolation_sd_prepare.py` and `interpolation_sd.py`.

Implementation commit: `b28b16c`; the subsequent evidence commit adds retained
hardware logs, quality tables, final report, additional worst-case diagnostics
and CI validation. Changed production files are `src/app/wav.h`, the new
`sample_interpolation.h`, and guarded diagnostic/reporting portions of
`app_main.cpp`; supporting changes are qualification header, PlatformIO modes,
CI, host/SD/capture tools and documentation. Project, accepted event, slicing,
Repeat, pitch table and transport formats remain unchanged.

The remote result for the final branch head is available at the draft PR's
Checks tab and is reported with its run link in the delivery message. Green CI
means implementation/regression checks passed; it does not override the
unqualified alternate hardware results or the pending listening/UI observation.
A production default upgrade requires complete inherited coverage, qualified
all-fractional worst-case performance and genuine listening evidence. Work
stops at M21; no antialias resampler, oversampling, time-stretch or new effects
were added.
