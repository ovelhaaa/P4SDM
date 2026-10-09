# M21.3 — sample layout and Linear qualification

**PARTIAL. Production remains Nearest, PCM cache off, Linear32 off, ordinary
independent WAV allocation. No M22 or interpolation promotion.**

Baseline: `246434cf7024f7158954014f9f0857f21d9a9b1e`, clean local main and
remote main verified equal before changes. Hardware is the connected dedicated
Guition JC4880P443 ESP32-P4 v1.3 on COM13. Clocks, SDK, PSRAM timing, ECC,
Project codec, musical scheduler and M20 UI are unchanged.
Read-only live registers report CPU 360 MHz, 32 MiB PSRAM, 200 MHz SDK and
effective PSRAM clock, 128 KiB L2 with 64-byte lines, hex read/write and DDR
enabled, ECC disabled. Effective PSRAM frequency is calculated from SDK source
frequency and live dividers; it is not an independent electrical measurement.

## Allocation contract and ownership audit

`samples.cpp::load_name` is the shared browser/Project restoration path. One
storage task validates RIFF/PCM16 mono or stereo at 44.1 kHz before allocation.
Decoded output is resident mono PCM16, at most 2,097,152 frames / 4 MiB. The
original request is `heap_caps_malloc(frames*2, SPIRAM|8BIT)`; it is already an
independent ordinary allocation, not a 16-Track slab. The ordinary heap supplies
useful PCM alignment but does not promise 64-byte cache-line alignment. The
4096-byte aligned static decode buffer stays off the storage stack. Metadata
uses `new (nothrow)`; neither decoding nor publication allocates on audio.

`Sample::data` originally equals the owning base; `Sample::allocation` is a
byte count, not a pointer. Payload accounting includes the staged sample and
the old resident until acknowledgement. Sixteen active allocations plus one
staging allocation can coexist. The budget retains 4 MiB free PSRAM and checks
the largest free block. Metadata and heap allocator headers also consume heap;
free-heap measurements, rather than payload alone, govern the reserve.

After decoding and waveform analysis, storage publishes through `Transfer`,
then waits for the release/acquire audio acknowledgement. `consume()` installs
the new sample and calls `Voice::assign`, stopping its old transport before
publishing retirement. Storage clears previews, frees the retired allocation
and updates its owned pointer. Seek/short-read failures free only the staged
sample; validation/metadata/allocation failures leave the resident untouched.
There is no loader cancellation or shutdown API to invent qualification for;
requests while storage/project work owns the task are rejected.

Project activation first acknowledges audio detachment and UI preview removal,
then storage retires all old ownership and restores references serially through
the same loader. Missing references remain named for the UI. Project V2 stays
52,704 bytes; no persistent Sample/PCM addresses are encoded.

The inherited M13/M212 fixture instead allocates one 2 MiB slab with 128 KiB
spacing. B has sixteen 65,536-frame views; C shortens the views while retaining
the same backing layout. This construction is not ordinary WAV residency.
The M14 4 MiB waveform fixture is temporary and freed at startup. M212 memory
diagnostics also allocate temporary slabs/separate buffers; Delay and display
allocations are separate consumers of the same PSRAM. No DMA buffer placement
is changed.

## Experimental placement and budget

All new placement flags are diagnostic and default to zero. Fixture controls
cover the inherited slab, independent normal allocations, independent aligned
bases plus Track*64-byte offsets, and reverse allocation order. Every fixture
preserves its original per-Track PCM values and accepted A/B/C setup; no live
Voice position, pitch, slice or event scheduling is rewritten.

The opt-in loader policies are ordinary placement (0), 64-byte alignment plus
Track*64-byte staggering (1), and 64-byte alignment only (2). Policy 1 adds at
most 1023 bytes per sample, policy 2 at most 63; the bound includes the alignment
correction. Up to 16 residents add 8,688 requested padding bytes in policy 1
(sum of each Track's bound), plus at most 1023 for staging. PCM remains 2-byte
aligned; experimental PCM views are 64-byte aligned. These are CPU-only sample
buffers, not DMA buffers.

`LoadedSample`, private to the experimental loader, extends unchanged `Sample`
with an explicit `PcmAllocation` owning base/view/size. `Sample::allocation`
still stores bytes. Release always passes `pcm.base` to the allocator and
deletes the derived metadata. The owner adds 12 bytes per metadata object on
the 32-bit device; heap rounding/headers are separate from requested padding.
No size field stores a pointer; `Voice`, accepted
events and project formats remain unchanged. Padding must fit the largest
block and free-minus-reserve checks in addition to the existing PCM limit.
The experimental loader also checks actual free PSRAM after allocation, so
allocator rounding and metadata fallback cannot consume the required reserve.
Diagnostics can report the owning block's usable size through the installed
SDK, always using the original base. Ordinary production builds retain their
existing Sample allocation/retirement.
No general-purpose allocator or large permanent 16-Track reservation is added.
The genuine 176400-frame samples can have `owner_usable_bytes=360436` even for
requests near 353000 bytes. The requested 1023-byte bound is not a claim about
allocator bucket rounding or total physical heap overhead. Captured owning
block sizes and actual free/largest snapshots are retained, and the added
post-allocation reserve check covers that discrepancy in the experimental path.

## Address and locality evidence

`sample_layout_diagnostics.h` records Track, CPU-visible PCM/base, requested
bytes, frames, residues modulo 64/128/256/4096/131072, requested capabilities,
external-RAM membership, free PSRAM and largest block. These are mapped CPU
addresses; no independently translated physical bus address is claimed.
Pairwise distances can be computed from the retained address rows.

The common kernel performs exactly 4,096 volatile PCM16 reads per repetition,
32 repetitions, in interleaved, voice-major and 32-frame gather orders. Warm
repeated passes and a separate 1 MiB eviction sweep have identical content
checksums within each resident set. Different WAV sets may have different
checksums; timing alone does not assert content equality. Eviction is outside
timing and does not prove every line cold. No hardware miss counter is claimed.

Standalone memory controls use this same kernel before audio/display startup:
aligned slab, staggered slab, independent ordinary allocation, reversed
allocation order, and independent alignment-only allocations. Physical SD
controls run on storage-owned genuine WAV allocations, with audio still
running; logs explicitly mark that concurrency. Interruption and timer costs
are included. Consequently their maxima cannot be interpreted as isolated
hardware load latency.

Completed standalone controls (`MEMORY`, no audio/display) have the following
means per 4096 reads over 32 repetitions; all 30 groups return checksum
4293374976. The sweep is outside timing and is not proof of a cold cache.

| Layout | Warm interleaved us | Warm voice-major us | Warm gather us | Swept interleaved us |
|---|---:|---:|---:|---:|
| 64-byte aligned slab / 128 KiB spacing | 2049.78 | 125.06 | 211.03 | 2052.06 |
| Slab with Track*64-byte staggering | 136.22 | 72.03 | 159.03 | 187.38 |
| Independent ordinary buffers | 136.47 | 70.09 | 158.56 | 196.91 |
| Independent reverse allocation order | 136.16 | 70.12 | 158.75 | 196.06 |
| Independent alignment-only buffers | 137.78 | 70.06 | 158.09 | 192.69 |

Alignment-only is favorable for this 65536-frame geometry but unfavorable for
the genuine same-size WAV geometry below. No universal alignment benefit is
inferred. The first memory capture lost part of its unpaced final USB line;
all original bytes remain in `MEMORY_ATTEMPT1`. Its replacement uses paced
summary output, a bounded completion wait and a matching rebuilt archive.

Physical sessions read existing `m19p_*.wav` files and write only the reserved
`M213_LAYOUT_QUAL` project. These files were generated for prior qualifications;
actual SD loading is proven separately from real instrument/vocal material or
listening. Tests cover sixteen same-size loads, mixed mono/stereo/short/large
files, repeated/reversed replacements, deliberately fragmented heap, bounded
memory pressure, failure recovery and sixteen-reference Project Load.
Incomplete attempts remain preserved and are not accepted sessions.

Completed physical comparisons establish that the problem is **not limited to
the synthetic fixture**. Sixteen same-size 176,400-frame WAVs loaded through
the ordinary production path gave 2714.09 us per 4096 interleaved reads versus
140.72 us voice-major. Staggering the genuine loader allocations gave 141.25 us
interleaved, with the same checksum. After repeated replacements, the equivalent
figures were 2626.72 and 204.44 us. All figures include concurrent audio and
32 repetitions; this is a large observed locality effect, not a miss counter.

Ordinary first-set addresses range from `0x482abdfc` (Track 0) to `0x487d3e38`
(Track 15). Tracks 1–15 advance by 360452 bytes; their low offsets move by four
bytes at a time. Exact address residues modulo 131072 are distinct, but rounding
to the configured 64-byte line reveals **21 matching pairs of line addresses
modulo 131072**. Staggering removes those pairs in the first set. The diagnostic
line-address comparison is evidence for the hypothesis, not independently
decoded cache-set indexing. Merely counting equal byte addresses modulo 128 KiB
would miss this real pattern.

Mixed-size ordinary WAV sets are much faster (roughly 278–464 us interleaved in
the captured warm groups), and changes in allocation lifetime alter placement.
Padding has a clear isolated benefit for similar-size loads but does not yet
establish a universal full-application win. Alignment-only placement worsened
the similar-size groups to 2820.62/3063.06 us and produced 18/21 line-address
matches. That physical session later reported a hardware brownout while loading
Track 13. Its unedited incomplete log is rejected; no software root cause,
voltage measurement or timing result for the unfinished playback is inferred.
The first production restore failed while USB reenumerated; the bounded retry
successfully restored production with mounted SD. Detector/clocks were untouched.

## PCM cache and mathematical controls

The reference remains 32 frames/scalar fill: 1,344 internal bytes for sixteen
device caches without counters. No new cache capacity is promoted. Existing
block invalidation, source and region checks, reverse/slice boundary clamping,
assignment stopping and Project detachment remain unchanged. Address reuse is
covered by explicit invalidation tests; no persistent cache lifetime is added.

Linear64, original Linear32 and unsigned-magnitude Linear32 remain independent
reference kernels. Inherited tests cover every Q16 phase at rails/sign/zero
controls, one million random tuples, exact Nearest output, unity, all MIDI
pitches, short regions, reverse, fades, release/choke and zero realtime
allocation. Magnitude and placement are tested separately and together; a
microbenchmark improvement is never added to unrelated application maxima.

## Detailed DSP investigation

The existing full-block profiler retains control, render, bookkeeping, I2S
write and complete-block attribution. New startup-only grouped diagnostics
separate raw PSRAM lookup, cache lookup/fill, Linear32/magnitude arithmetic,
Voice lookup/fades/transport, filter processing, velocity/pan/mix/send,
Delay processing, output conversion and fade identity controls. Each measured
group has one timer pair for 4096 operations; there are no per-sample timers.
An input/dispatch control and 32 empty timer pairs quantify included harness
cost. These grouped component measurements include dispatch/input overhead,
do not reconstruct full-engine cost additively and do not supply direct
per-stage counters inside the accepted render.

The completed `B_PROFILE` capture records these startup group means (each group
does 4096 operations): input/dispatch 336.75 us, PSRAM lookup 439.13 us, cache
lookup/fill 673.59 us, original Linear32 601.63 us, magnitude Linear32 532.59 us,
Voice Nearest 1452.47 us, Voice Linear 2274.41 us, filter 525.75 us,
velocity/pan/mix/send 802.16 us, Delay 1736.94 us, output conversion 458.41 us,
fade reference 393.34 us and exact fade identity 338.53 us. The 32 empty timer
pairs total 24 us. Matching arithmetic/fade checksums corroborate equivalence;
Voice Nearest and Linear have different expected interpolation outputs.

Within that profiled cached B application, measured block means/maxima are
control 25/84 us, render 4623/4939 us, bookkeeping 61/132 us, output write
1086/1382 us and complete interval 5796/5874 us. Histogram percentiles are
100-us-bin upper bounds, distinct from the exact M5 timing quantiles. Its M5
maximum is 4973 us versus 4990 us for unprofiled cached magnitude B; this paired
control does not establish negative instrumentation overhead. Startup stages
run before audio and stage histograms add timers to the application. No sums
of unrelated maxima are used for qualification.

M5 processing ends before `audio::write`; that write contains HAL downmix and
blocking I2S delivery. The startup conversion group measures master gain/clip,
not a separate HAL mono downmix counter. Direct DMA underrun/backlog counters
are unavailable; write failures/timeouts are the recorded output indicators.

Filter coefficients are already refreshed only on accepted tone changes via
`VoiceState::apply`; repeated accepted pairs are counted as avoided work.
Delay time/feedback/input level are already stored parameters. Velocity gain,
pan gains and accepted Delay sends are resolved outside their arithmetic hot
path. Filter histories and Delay feedback reads/writes must still advance each
sample. Moving event-dependent work to a block boundary could change musical
semantics and is not introduced without measurements and an equivalence proof.

## Complete application captures

The deadline is 5804.988662 us; 20% reserve requires integer maximum <=4643 us.
`m213_results.json` retains p50/p95/p99/max, headroom, misses, I2S counters,
completeness, faults, memory snapshots and strict B/C digest/residency checks.
Rejected configurations stay visible. Initial completed controls:

| B, 120 seconds, Nearest, cache off | p50 us | p95 us | p99 us | max us | headroom | reserve |
|---|---:|---:|---:|---:|---:|---|
| Original 128 KiB fixture spacing | 5361 | 5540 | 5608 | 5693 | 1.93% | fail |
| Independent normal allocations | 3381 | 3599 | 3651 | 3740 | 35.57% | pass |
| Aligned independent staggered bases | 3378 | 3600 | 3651 | 3728 | 35.78% | pass |
| Reverse-order independent allocations | 3388 | 3610 | 3660 | 3756 | 35.30% | pass |

All four complete 20,672 blocks, 126,480 accepted events, 230 Chain cycles,
7,168 x8 hits and 5,292,032 active frames per Track, with digest 978631659.
They have zero observed deadline misses/I2S failures/timeouts and matching
Nearest output summary. The normal/staggered runs retain equal PSRAM/internal
heap snapshots. The original control's internal heap increases by 528 bytes;
this is retained as a variation, not asserted identical heap stability.

| Requested comparison | Evidence |
|---|---|
| Original Nearest | Fresh B current capture above |
| Nearest + cache | Historical M212 B 4290 us; explicitly historical |
| Nearest + improved PCM layout | Fresh B normal/stagger controls above |
| Nearest + layout + cache | Fresh B normal: 4339 us; slower than direct 3740 us |
| Linear32 + cache | Historical M212 B 5089 us; explicitly historical |
| Linear32 + layout + cache | Fresh B normal: 4986 us; reserve fail |
| Optimized Linear32 + best tested placement/cache combination | Fresh staggered/cache-off B 4099 / C 4104 / A 4029 us; normal/cache-off B 4105 / C 4117 / A 4074 us; production eligibility remains pending |

Original Linear32 with normal independent placement and no PCM cache measured
4242 us in B. Magnitude plus cache measured 4990 us, so its isolated arithmetic
win is not a cached full-application win. Direct magnitude B/C retain the
accepted hashes, complete 230 Chain cycles/7168 x8 hits and all-voice residency.
Direct magnitude A completed one inherited Chain cycle and passes the unchanged
M20/M19/M18.1 analyzers, including the complete musical/UI/transient workload.
Its wall-clock UI commands do not establish an identical complete A digest.

| Candidate / scenario | p50 us | p95 us | p99 us | max us | reserve |
|---|---:|---:|---:|---:|---|
| Normal direct original Linear32 B | 3903 | 4115 | 4159 | 4242 | pass |
| Staggered direct original Linear32 B | 3900 | 4107 | 4158 | 4234 | pass |
| Normal direct magnitude B | 3768 | 3992 | 4033 | 4105 | pass |
| Normal direct magnitude C | 3761 | 3988 | 4035 | 4117 | pass |
| Normal direct magnitude A | 2855 | 3711 | 3834 | 4074 | pass |
| Staggered direct magnitude B | 3764 | 3980 | 4024 | 4099 | pass |
| Staggered direct magnitude C | 3758 | 3978 | 4026 | 4104 | pass |
| Staggered direct magnitude A | 2851 | 3693 | 3818 | 4029 | pass |
| Staggered cached original Linear32 B | 4625 | 4829 | 4884 | 4991 | fail |
| Staggered cached magnitude B | 4621 | 4832 | 4887 | 5017 | fail |

B/C each use 20672 blocks; A uses its inherited 13782-block interval. Both
staggered B/C traces match the same baseline host digests and activity counts.
Staggered A also passes the unchanged inherited M20/M19/M18.1 analyzers,
including its one Chain cycle. Small differences between normal and staggered
maxima do not prove a repeatable full-renderer placement improvement. Direct
lookup consistently outperforms the 32-frame cache for these independent
allocations. Magnitude arithmetic is tested separately before combination.

Physical ordinary/cache/magnitude playback recorded 7910 measured blocks,
max 4397 us, zero misses/failures/timeouts and 16 active voices. Its sparse initial
Repeat window had zero hits, so it is not counted as Repeat coverage. The
stagger/cache/magnitude session recorded 18251 blocks, max 4505 us and 40560
Repeat hits, also without misses/failures/timeouts. Its stored p99 covers only
the first 16384 timings; the maximum covers the whole interval. These different
musical windows cannot prove staggering improved full-engine performance.

The completed staggered/cache-off genuine-WAV session records 17920 measured
blocks, p99 3664 us over the first 16384 stored timings, maximum 3794 us over
the complete interval, zero misses/failures/timeouts, 16 simultaneously active
voices, 23664 actual Repeat hits and 12 physical Chain loops. It passes mono,
stereo downmix, large/short samples, invalid/truncated/missing files, repeated
replacement, deliberate fragmentation/pressure and recovery, reverse, Slice
Locks, Gate/choke, transient Cancel/Apply, and two successful sixteen-reference
Project restorations. Project decoder acceptance proves the existing V2 length
and CRC contract; the reported file size remains 52704 bytes. Address snapshots
show disjoint PCM ranges. This real mixed-size window is not the continuously
active 16-voice B/C trace fixture.

Fresh dense physical Repeat components independently record ordinary/cache-on
max 4616 us (3549 blocks, 32368 Repeat hits) and staggered/cache-off max 3718 us
(2384 blocks, 18544 hits), each with 16 active voices and zero output/deadline
errors. They have different elapsed musical windows and logging builds, so
these are separate safety observations, not an exact event-equivalent timing
gain claim. The first ordinary full SD session's zero Repeat remains visible;
this follow-up supplies positive real-WAV Repeat coverage.

Original logs and artifact manifests are retained. The first matrix dispatch
stopped before its second flash because PlatformIO removed unselected build
directories when another configuration was built. Production was restored.
Reproduction now builds and archives each environment immediately, then flashes
hash-verified immutable archives. No rebuilt ELF is labeled as matching an old
capture. A partial physical session lost a read-only STATE command fragment
during Project Load; it is preserved as ATTEMPT1, and query retries do not
repair or fabricate that log.
The first direct physical session also stopped after a missing `[M6 load]`
confirmation following `[M6 read]`; it has no complete host marker. The burst
USB transport is a suspected cause, not a proven software/PCM diagnosis. The
attempt and host traceback are retained. A subsequent diagnostic-only console
copy paces and serializes storage/UI output with a startup-created mutex;
audio never takes it. Its firmware is archived separately. This instrumentation
change prevents treating the old/new physical maxima as a pure allocator A/B.

## Host, CI, sustained operation and production decision

The inherited 92-command host sweep produced 91 successes plus a Windows
temporary-executable deletion error after the UI equivalence assertions had
passed. That exact UI command passed independently on repetition. The new
placement test exercises all 64 possible heap-base residues, padding canaries,
allocation failure, owner-only free, double-release prevention, real Transfer
acknowledgement/retirement, mixed sizes, allocation order, fragmentation and
pointer reuse. Linux CI additionally runs ASan/UBSan. The capture analyzer tests
explicitly reject 4644 us, incomplete summaries, missing voice/event evidence,
watchdogs and I2S failures. Inherited analyzers are unchanged.

A ten-minute B diagnostic retains every timing in startup-allocated PSRAM
arrays and periodically reads all PCM on storage, with checksums and heap
snapshots. Its independent host reference expects 618,000 events, 1,190 Chain
cycles, 7,168 x8 hits and 26,460,160 active frames per Track, digest 2738615385.
Competing integrity reads are included in the actual application workload;
they cannot be subtracted from its maximum. The completed normal-placement,
cache-off magnitude B run measured p50/p95/p99/max 3772/3993/4037/4144 us across
103360 blocks. Its exact expected trace matches, all sixteen voices retain
26460160 active frames, all integrity checks report zero corruption, and free
PSRAM/largest block/internal heap before and after are exactly equal. The
staggered run also completed with p50/p95/p99/max 3770/3976/4021/4113 us, the
same expected long trace, all active-frame counts, zero corruption and exactly
equal before/after heaps. These maxima retain 28.61% and 29.15% headroom,
respectively. Extended SYNTH qualification also passed 103360 blocks:
p50/p95/p99/max 3138/3906/4118/4545 us, zero misses/failures/timeouts, no reboot
or watchdog, and identical before/after heaps. Periodic monitoring confirms no
sample residency in this SYNTH build and constant free/largest/internal heap.
Its maximum retains 21.70% headroom. The long interval contains a different
proportion of the inherited UI phases than the 80-second control; the lower
median is not claimed as an optimization gain.

The fresh inherited 80-second SYNTH control passed the unchanged M20 and
inherited analyzers: p50/p95/p99/max 3755/4136/4242/4542 us, zero observed
misses/failures/timeouts and equal before/after heaps. Remote implementation
CI [run 37990206303](https://github.com/ovelhaaa/P4SDM/actions/runs/37990206303)
completed SUCCESS for `8d1a0ea9a3014e4fb4f11e6472143696402b2dbe`, including all
inherited checks, new ownership ASan/UBSan, SD variants and expanded builds.
Final evidence-head CI is also required before a full acceptance claim; its
exact SHA/run is recorded in the PR validation metadata after completion.

The central finding is that allocation lifetime and sample size can create
harmful locality in genuine WAV residency as well as in the slab fixture.
Independent normal allocations recover substantial synthetic renderer
performance without the extra PCM cache; bounded staggering greatly improves
the same-size genuine WAV lookup control. The selected direct magnitude builds
pass A/B/C timing, both ten-minute SAMPLE runs, extended SYNTH and the complete
physical SD candidate session. These are engineering qualifications, not full
production acceptance: actual musical A/B listening and full A immutable event
equivalence remain pending. A padded Nearest-only A/C matrix was not separately
qualified, so a Nearest allocator promotion is not inferred either. Production
defaults stay intact and the final archived production image was successfully
restored with mounted SD and configured audio.

The five acceptance answers are: (1) harmful locality occurs in genuine WAV
residency; (2) bounded staggering removes a large same-size lookup penalty and
the selected ownership/fragmentation/Project tests pass; (3) independent direct
lookup outperforms the 32-frame cache in the complete tested fixtures, while
padding adds no demonstrated large universal renderer gain; (4) optimized
Linear exceeds 20% headroom in continuous 16-voice B/C and inherited A timing,
with complete B/C trace equivalence; (5) selected extended and physical safety
tests pass, but production remains PARTIAL because A equivalence and human
listening are incomplete. The alignment-only brownout cause, independent DMA
underrun counters, physical cache-miss counts and musical instrument/vocal WAV
listening also remain outside the established evidence.

Reproduction: `python tests/m213_checks.py`, `python tests/m213_capture_test.py`,
the inherited interpolation/cache/magnitude/trace checks, then
`python tests/m213_build.py <cases>` and
`python tests/m213_archived_device.py <cases>`. Physical console preparation is
`python tests/m213_sd_prepare.py`, followed by the three diagnostic SD builds,
artifact archiving and `python tests/m213_sd_device.py 0 1 2` on the dedicated
device. `python tests/analyze_m213.py --require-case B_NORMAL` verifies that
positive control; `--require-acceptance` fails while acceptance gaps remain.
CI additionally requires the passing A/B/C, sustained and genuine SD captures,
the complete standalone memory controls, and explicit rejection of completed
over-budget controls. Firmware/ELF/MAP/bootloader/partition/config hashes and
source/SDK manifests are retained in `m213_firmware_inventory.json`; matching
binaries remain in the ignored local `.pio/m213-artifacts` archives. Raw M213
logs use Git `-text` attributes to preserve captured bytes across platforms.
Host script hashes in that inventory describe the final reproduction tools,
not a retroactive assertion about every earlier attempt's script version.
Manifests record source-file state at archiving; ELF/BIN/MAP hashes identify
the exact captured firmware. Initializing boot/restore logs are not full stress
qualifications. Some short captures free an additional 528 internal bytes
during startup cleanup; both extended SAMPLE runs and extended SYNTH retain
exactly equal heaps and periodic snapshots.

No clock change, DSP reorder, larger production cache, UI selector, persistent
format change, Hermite optimization, antialias resampler or new audio feature
is included. Work stops at M21.3.
