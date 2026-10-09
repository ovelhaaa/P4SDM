# M21.3 — sample layout and Linear qualification

**PARTIAL. Production remains Nearest, PCM cache off, Linear32 off, ordinary
independent WAV allocation. No M22 or interpolation promotion.**

Baseline: `246434cf7024f7158954014f9f0857f21d9a9b1e`, clean local main and
remote main verified equal before changes. Hardware is the connected dedicated
Guition JC4880P443 ESP32-P4 v1.3 on COM13. Clocks, SDK, PSRAM timing, ECC,
Project codec, musical scheduler and M20 UI are unchanged.

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
deletes the derived metadata. No size field stores a pointer; `Voice`, accepted
events and project formats remain unchanged. Padding must fit the largest
block and free-minus-reserve checks in addition to the existing PCM limit.
Ordinary production builds retain their existing Sample allocation/retirement.
No general-purpose allocator or large permanent 16-Track reservation is added.

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

Physical sessions read existing `m19p_*.wav` files and write only the reserved
`M213_LAYOUT_QUAL` project. These files were generated for prior qualifications;
actual SD loading is proven separately from real instrument/vocal material or
listening. Tests cover sixteen same-size loads, mixed mono/stereo/short/large
files, repeated/reversed replacements, deliberately fragmented heap, bounded
memory pressure, failure recovery and sixteen-reference Project Load.
Incomplete attempts remain preserved and are not accepted sessions.

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

All three complete 20,672 blocks, 126,480 accepted events, 230 Chain cycles,
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
| Nearest + layout + cache | Qualification in progress |
| Linear32 + cache | Historical M212 B 5089 us; explicitly historical |
| Linear32 + layout + cache | Qualification in progress |
| Optimized Linear32 + best safe configuration | Qualification in progress |

Original logs and artifact manifests are retained. The first matrix dispatch
stopped before its second flash because PlatformIO removed unselected build
directories when another configuration was built. Production was restored.
Reproduction now builds and archives each environment immediately, then flashes
hash-verified immutable archives. No rebuilt ELF is labeled as matching an old
capture. A partial physical session lost a read-only STATE command fragment
during Project Load; it is preserved as ATTEMPT1, and query retries do not
repair or fabricate that log.

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
they cannot be subtracted from its maximum. Device and fresh SYNTH qualification
are in progress. Remote CI status is pending and must become SUCCESS before
any full acceptance claim.

The central finding so far is that the pathological slab is a fixture artifact
and independent ordinary allocation already recovers substantial performance
without the extra PCM cache. Whether a permanent padded loader policy helps
representative genuine WAV residency must follow from the physical comparisons,
not from that synthetic improvement. Actual musical A/B listening and full A
immutable event equivalence remain pending. Linear promotion cannot proceed
without listening even if B/C timing passes. Production defaults stay intact.

Reproduction: `python tests/m213_checks.py`, `python tests/m213_capture_test.py`,
the inherited interpolation/cache/magnitude/trace checks, then
`python tests/m213_build.py <cases>` and
`python tests/m213_archived_device.py <cases>`. Physical console preparation is
`python tests/m213_sd_prepare.py`, followed by the three diagnostic SD builds,
artifact archiving and `python tests/m213_sd_device.py 0 1 2` on the dedicated
device. `python tests/analyze_m213.py --require-case B_NORMAL` verifies that
positive control; `--require-acceptance` fails while acceptance gaps remain.

No clock change, DSP reorder, larger production cache, UI selector, persistent
format change, Hermite optimization, antialias resampler or new audio feature
is included. Work stops at M21.3.
