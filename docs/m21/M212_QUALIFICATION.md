# M21.2 — PCM cache, Linear and PSRAM qualification

**PARTIAL. Production remains Nearest, cache disabled, Linear32 disabled.**
No M22, project migration, UI redesign, adaptive interpolation, clock change,
ECC change, watchdog change or platform replacement is included.

## 1. Baseline and matching firmware

Baseline is merged main `59e1da5c223206e3505f74e752cb1f5fc82a8afb` (PR #14).
Fresh captures use `GUITION_M212_*` names; historical M21/M21.1 evidence is
unchanged. `m212_builds.json`, `m212_memory_build.json` and
`m212_matrix_builds.json` and `m212_experiment_builds.json` identify exact
ELF/BIN/MAP/bootloader/partition hashes.
Matching artifacts and manifests are archived **before flash** outside PlatformIO
build directories, under `.pio/m212-artifacts`. Full manifests include dirty
source hashes, SDK hash and package versions. These local archives must accompany
any subsequent fault decoding; CI artifacts are different builds.

## 2. Actual memory configuration

Installed platform is pinned pioarduino `55.03.36-1`; Arduino is 3.3.6,
framework libraries IDF 5.5.0 `f56bea3d1f`, RISC-V toolchain 14.2.0.
`chip_variant=esp32p4_es` selects the ES framework. The installed ES `sdkconfig`
and `qio_qspi/include/sdkconfig.h` specify:

```
CONFIG_SPIRAM_MODE_HEX=1
CONFIG_SPIRAM_SPEED_200M=1
CONFIG_SPIRAM_SPEED=200
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360
CONFIG_CACHE_L2_CACHE_SIZE=0x20000
CONFIG_CACHE_L2_CACHE_LINE_SIZE=64
```

Physical diagnostics report revision **103 (v1.3)**, CPU **360 MHz**, and
**33,554,432 bytes** PSRAM. Live HEX read/write bits and DDR bit are all 1.
The board JSON `psram_type=qspi` is a framework packaging label, not proof of a
QUAD PSRAM data interface. The effective device interface is **16-line HEX DDR**.
Exact manufacturer/device part number is **not independently verified**.

## 3. Effective 200 MHz operation

`memory_audit.h` reads the installed target's `HP_SYS_CLKRST` and `SPIMEM2`
definitions without writing registers. Installed `psram_ctrlr_ll.h` establishes
source selector 1 = MPLL, core divider = field+1, bus divider = field+1 unless
the equality bit selects unity. `esp_clk_tree_src_get_freq_hz` is an actual
installed SDK API, used for the source frequency:

```
source=1 source_hz=400000000 source_status=0
core_div=1 bus_div=2 effective_hz=200000000
hex_read=1 hex_write=1 ddr=1 ecc=0
```

Thus the live SDK clock path verifies **200 MHz**, independently of the JSON
declaration. Electrical frequency at the memory pins is **not independently
verified**. The IDF 5.5 P4 [Kconfig](https://github.com/espressif/esp-idf/blob/v5.5/components/esp_psram/esp32p4/Kconfig.spiram)
offers 20/80/200 MHz and HEX mode; 200 MHz requires experimental features, enabled
in this installed SDK. Espressif's [v1.3/v3.2 change notice](https://documentation.espressif.com/en/PCN202600801_ESP32-P4_Chip_Revision_v3.2_Upgrade_Chip_Revision_v1.3_Demand_Collection_and_EOL_Plan_Description.html)
identifies 200 MHz for v1.3. Nothing here justifies 250 MHz on this board.

## 4. L2, ECC, mapping and boot

L2 is configured to **128 KiB**, with **64-byte lines**. Runtime L2 size/line
registers were not independently decoded. ECC is disabled in the supplied SDK,
and the live 16-to-18 ECC bit is 0; it was not disabled for benchmarking.
XiP, instruction relocation and rodata relocation to PSRAM are absent from the
effective SDK configuration. Runtime XiP mapping is not independently decoded.
PSRAM uses `CONFIG_SPIRAM_USE_MALLOC`, with the 4,096-byte always-internal
threshold; BSS/noinit relocation is disabled. Actual benchmark addresses are
in the `0x48......` external mapping.

The bootloader binary is archived and unchanged. `esptool image-info` on the
restored production bootloader reports **DIO, 80 MHz, 16 MB flash**, matching the
inherited flash command. The supplied SDK header contains both the QIO selection
macro and a `"dio"` flash-mode string; the binary header establishes the actual
bootloader setting. This flash setting is separate from the HEX PSRAM interface.
The SDK disables
`SPIRAM_BOOT_HW_INIT`; Arduino `esp32-hal-psram.c` calls `esp_psram_init`, the
external RAM test and heap registration. The installed PSRAM library references
`mspi_timing_psram_tuning`; calibration was not bypassed. Display HAL explicitly
checks external framebuffer addresses. Display buffers and PCM share PSRAM;
the non-realtime memory test intentionally starts neither display nor audio.

## 5. PSRAM bandwidth and locality

See `GUITION_M212_MEMORY_FINAL_SERIAL.log` and `m212_results.json`. Streaming
copies transfer 2 MiB per repetition, 32 repetitions, with a 16 KiB internal
buffer; the CPU read test uses volatile 32-bit reads. Rates are decimal MB/s,
including CPU/copy overhead, **not theoretical bus bandwidth**.

| Streaming operation | Bytes | Total us | Effective MB/s |
|---|---:|---:|---:|
| PSRAM → internal memcpy | 67,108,864 | 581,278 | 115.451 |
| Internal → PSRAM memcpy | 67,108,864 | 747,833 | 89.738 |
| Sequential CPU 32-bit reads | 67,108,864 | 646,653 | 103.779 |

The locality controls read the same 4,096 PCM16 values per repetition. Every
layout/order control has the same checksum. Orders are interleaved, voice-major,
32/64/128-frame gathering. Base addresses and alignment are printed. Repeated
same-region passes are warm candidates; a separate 1 MiB cache-eviction sweep
occurs outside timing for the eviction candidates. This does not prove all
cache lines cold; no cache invalidation API or miss counter is claimed.

## 6. Alignment and cache conflicts

Layout 0 is one allocation with 128 KiB-spaced, 64-byte-aligned bases. Layout 1
staggered each voice by another 64 bytes, retaining identical per-voice content.
Layout 2 uses separate aligned allocations; layout 3 uses ordinary PSRAM
allocation. All preserve the same logical indices and read count.

Aligned interleaving versus staggered interleaving changes throughput dramatically.
Together with the voice-major control this strongly supports locality/set
contention as a dominant cost. **Cache-set conflict is an inference, not direct
counter proof.** The M21.1 original benchmark remains available unchanged;
its staggered control used different PCM indices and cannot establish identical
content by itself.

| Identical-content warm control | Mean us / 4,096 reads | MB/s |
|---|---:|---:|
| Aligned contiguous, interleaved | 2,040.875 | 4.014 |
| Same aligned bases, voice-major | 124.094 | 66.015 |
| Same aligned bases, 32-frame gather | 201.063 | 40.744 |
| Same aligned bases, 64-frame gather | 199.063 | 41.153 |
| Same aligned bases, 128-frame gather | 198.094 | 41.354 |
| Staggered contiguous, interleaved | 111.906 | 73.204 |
| Separate 64-byte aligned, interleaved | 111.938 | 73.184 |
| Separate ordinary allocation, interleaved | 109.219 | 75.005 |

## 7. Cache capacity and fill strategies

The opt-in cache supports 16/32/64/128 frames and scalar/memcpy/four-load unrolled
fills. Default experimental capacity/fill remain 32/scalar. Each fill is bounded
by the current region. No allocation, DMA, filesystem access or lock is added.
Optional hit/miss/read counters are disabled in normal builds.

Without counters, 16 device caches require respectively **832 / 1,344 / 2,368 /
4,416 bytes**; counted variants add 192 bytes. One 32-frame PCM buffer spans
64 bytes, matching a configured L2 line when aligned; internal cache values are
not guaranteed to share physical alignment with every sample slice.

The non-realtime fill matrix covers all capacities and three strategies; its
consumer and checksum are identical within that matrix. Streaming writes precede
this isolated matrix, so its checksum is intentionally distinct from the earlier
locality controls. `memcpy` wins for 32–128 frames in this capture. The separate
full-application matrix uses Linear32 B, all four capacities, scalar/memcpy,
60-second intervals, counted cache accesses and the block profiler. Counter and
profiler overhead are included. These experiments do not replace 120-second
A/B/C qualification. Prefer the smallest proven configuration; no promotion
follows from a startup win.

| Counted/profiled Linear32 B, 60 s | Max us | Hit rate | PCM16 fill reads | Device cache bytes |
|---|---:|---:|---:|---:|
| 16 scalar | 5,212 | 96.670% | 45,080,064 | 1,024 |
| 16 memcpy | 5,242 | 96.670% | 45,080,064 | 1,024 |
| 32 scalar | 5,192 | 98.122% | 50,838,528 | 1,536 |
| 32 memcpy | 5,214 | 98.122% | 50,838,528 | 1,536 |
| 64 scalar | 5,210 | 98.848% | 62,355,456 | 2,560 |
| 64 memcpy | 5,188 | 98.848% | 62,355,456 | 2,560 |
| 128 scalar | 5,282 | 99.369% | 68,321,280 | 4,608 |
| 128 memcpy | 5,202 | 99.369% | 68,321,280 | 4,608 |

All eight share digest `2005073613`, 65,040 events, 110 Chain cycles and 7,168 x8
hits. Their hit rates improve with capacity, but read amplification also grows.
These counts are logical PCM16 loads requested by fills, **not physical PSRAM
bus transactions**. No larger capacity or memcpy strategy earns promotion on
these complete-block measurements. The best maximum differs by only 4 us from
32/scalar; that does not establish a reproducible win. Unrolled full-application
fills remain unmeasured. Isolated fill times are preserved in the raw log/JSON.

## 8. Exact workload descriptions

**A:** unchanged inherited M20/M21 mixed SAMPLE workload, including all existing
project, Chain, performance, Repeat, transient and UI fixtures. The unsafe
per-block fractional macro is not enabled.

**B:** sixteen independent 65,536-frame PSRAM SAMPLEs; every region/slice is full,
MIDI 59, forward OneShot, fractional Q16 transport, dense four-ratchet accepted
parents, per-voice filtering, pan/mix and Delay. All pattern locks are configured
before any parent is accepted. A two-entry looping Chain, Override, Fill and x8
Repeat use `Engine::apply` commands. Duration is 20,672 blocks (120 seconds).

**C:** same command schedule, varied pitches 47–70 (below/above unity, including
integer controls), mixed phases, alternating forward/reverse, varying PSRAM
sample lengths 65,536 down to 50,176, and distinct locked slice regions.
All sixteen voices stay active in the deterministic fixture. B/C use the normal
display/touch tasks and output path; they do not claim inherited UI20 stress or
physical touch coverage. Startup private pattern construction is excluded from
timing; there is no per-block active Voice transport override.
The old forced-fractional rewrite and its preparation-excluded timing are removed.
Legacy environment names still compile, but report deprecation and cannot be
used to reproduce historical forced transport without the historical Git source.

## 9. Event, Chain and Repeat equivalence

`m212_trace_checks.py` compares accepted event fields, routing, pitch, playback,
RNG, Chain/Repeat state and Voice transport in deterministic Nearest/Linear32,
cache-off/cache-on host executions. PCM output intentionally differs. Every
120-second host fixture has **126,480 events, 230 Chain cycles and 7,168 x8 hits**.
B/C device summaries capture the same bounded control digest and per-voice
active-frame counts. First-block pre-render fractional occupancy is initially
zero; accepted triggers make all voices contribute during that block.

The inherited A script constructs a 32-entry Chain after 24 seconds, performs
further Chain edits around 49 seconds, disables looping, and the isolated Repeat
fixture switches to pattern 15 after block 11,000. UI/project activity also
uses wall-clock scheduling. These competing windows explain why capture length
alone does not guarantee matched completed cycles. A semantics were preserved;
no full event equivalence or complete Chain coverage is inferred from them.

## 10. DSP profiling and optimization decision

No timer was added per audio sample. The existing bounded block profiler measures
control, aggregate render, bookkeeping, I2S write and complete block. Counted
matrix builds reuse it and report stack margin. **Cache/interpolation/fades,
filters, pan, sends, Delay and conversion substage attribution is still missing.**
Counter/profiler/trace overhead cannot be subtracted by subtracting unrelated
maxima. The 60-second profiled/counting matrix and 120-second non-profiled B
capture have different interval lengths. No claimed full-DSP optimization or
unprofiled production performance is derived from their difference.

An independently tested exact rounding candidate is now available through
`P4SDM_LINEAR_MAGNITUDE=1` together with `P4SDM_LINEAR_32BIT=1`. It retains
`x0*(65536-f)+x1*f`, computes an unsigned absolute magnitude, adds 32,768,
shifts by 16 and restores the sign. Even `INT32_MIN` is safe: the unsigned
magnitude plus rounding bias is below `UINT32_MAX`. The accepted old int32
and int64 kernels remain unchanged as independent references.

Actual pinned-toolchain disassembly of the outlined old int32 kernel contains
**`div` and `rem`** with the 65,536 divisor. The new kernel contains two multiplies,
sign/magnitude operations and a logical right shift, with neither division nor
remainder. Outlined sizes are 50 versus 44 bytes; both application render functions
are 8,654 bytes (`0x21ce`). This differs from historical observations about the
int64 reference and from host compiler strength reduction. Disassembly is
preserved locally as `.pio/m212-linear32-kernel.asm` and
`.pio/m212-magnitude-kernel.asm`. Host checks sweep all 65,536 phases for 36
rail/sign/zero tap pairs and one million random tuples against Linear64, and
compare the complete B/C transport traces. Device arithmetic, startup Voice
renderer and 120-second full-application B are measured independently. No
microbenchmark-only result changes the defaults.

Two further independent diagnostic options are available. `P4SDM_PCM_CACHE_VALIDATED=1`
lets already-validated Voice taps skip the duplicate null/region/frame checks;
the public cache `read()` remains guarded. `P4SDM_FULL_GAIN_FASTPATH=1` skips
the multiply/divide when the resolved fade gain equals its divisor, an exact
identity. Attack, remaining/end fade, release and transport updates remain
unchanged. Render disassembly confirms a hardware divide in the old fade path.
The full-gain oracle additionally releases at frame 193 to cover steady full
gain, as well as the inherited early release at frame 17.

## 11–13. Application performance and headroom

Fresh maxima, quantiles, misses and exact reserve checks are in `m212_results.json`.
Deadline is `256*1e6/44100 = 5804.988662... us`; the 20% reserve limit is
`4643.990929... us`. Integer-microsecond maxima must be **<= 4643**. p99 or average
cannot override a failing maximum. Each result belongs only to its own workload.

| 32-frame scalar cache | Max us | Headroom | Reserve | Chain cycles |
|---|---:|---:|---|---:|
| A Nearest | 4,272 | 26.41% | pass | 0 |
| A Linear32 | 4,928 | 15.11% | fail | 0 |
| B Nearest | 4,290 | 26.10% | pass | 230 |
| B Linear32 | 5,089 | 12.33% | fail | 230 |
| C Nearest | 4,267 | 26.49% | pass | 230 |
| C Linear32 | 5,052 | 12.97% | fail | 230 |

All six captured zero processing deadline misses, I2S failures and timeouts.
B/C control digests match host, and all sixteen residency counts equal
5,292,032 output frames. Linear32 B misses the reserve by **445.009 us**;
C misses it by **408.009 us**. Neither A capture qualifies Chain coverage.

| Independent 120-second B experiment, 32/scalar cache | Max us | Headroom | Reserve shortfall us |
|---|---:|---:|---:|
| Unsigned-magnitude Linear32 rounding | 4,949 | 14.75% | 305.009 |
| Validated cache taps, original Linear32 | 4,966 | 14.45% | 322.009 |
| Full-gain identity, original Linear32 | 5,013 | 13.64% | 369.009 |

All three retain the exact B digest, full residency and complete Chain/Repeat
coverage, with zero observed watchdogs/panics/misses/I2S failures/timeouts. No
combined win is inferred by summing independent maxima. The magnitude arithmetic
control measured 8,908 us versus 9,345 us for old int32 over 65,536 iterations;
its incremental XOR checksum matches (3,394). The complete B maximum improves
by 140 us, but still breaches the reserve. Experiments remain opt-in, disabled
in production; their A/C, physical WAV and extended regressions are pending.

## 14. Watchdog, I2S and DMA findings

Fresh logs are checked for watchdog/panic markers and complete summaries. Audio
processing timing excludes the subsequent blocking I2S write. Existing profiling
includes downmix and blocking write together. I2S failures/timeouts are counted;
zero failures does not independently prove zero DMA underruns. DMA backlog,
wakeup delay and idle-task residency remain unmeasured. No scheduler yield,
watchdog timeout or watchdog disabling was added to audio.

The historical Store fault remains provisionally associated with a USB/JTAG
store using a rebuilt, nonmatching ELF. Its original ELF is unavailable, so its
root cause is unresolved. New exact artifact archives support future decoding;
no speculative USB/IDF/PCM ownership fix is included.

## 15. Heap, PSRAM and stack stability

Fresh summaries retain free PSRAM/internal/largest-allocation before/after
snapshots and per-voice residency. Equality is a bounded snapshot observation,
not continuous heap-drift or sample-integrity proof. Counted/profiled builds
report audio stack high-water. 120-second B/C intervals exceed the previous
approximately 15-second watchdog interval, but do not replace a longer soak
with continuous heap/PCM integrity monitoring.
Five main captures have equal before/after snapshots; C Nearest's internal free
heap increases by 528 bytes (161,168 → 161,696). This is recorded as a variation,
not certified heap equality or a demonstrated leak cause.
Validated-tap and full-gain B also show a 528-byte internal free-heap increase;
magnitude B has equal snapshots. Counted matrix stack high-water values range
from 6,440 to 6,552 bytes for the 8,000-byte audio stack. For example, 16/scalar
profiling reports mean control/render/bookkeeping/write/complete times of
15/4,900/47/833/5,797 us. Rendering dominates processing; the write duration is
largely legitimate pacing. Its complete-block maximum of 5,859 us includes that
blocking interval and cannot be compared directly with a processing-only maximum.

## 16. Physical SD validation

The mounted physical SD and nine indexed files are visible in fresh application
boot logs. Resident PSRAM fixtures are synthetic; mounting/indexing actual WAVs
does **not** qualify their playback/replacement. New cache-enabled physical WAV
load/replacement, reverse, Slice Locks, Gate/choke, Repeat, transient Apply/Cancel,
Project Save/Load and retirement/heap recovery remain pending. No unrelated user
file is intentionally rewritten. Historical physical-SD results remain historical.

## 17. Project V2, SYNTH and UI

Persistent model/codec sources are unchanged: **V2, 52,704 bytes**. Existing exact
codec/event/RNG/Repeat/lock comparisons pass. Voice remains 64 bytes. Synth source
and filter/Delay DSP are unchanged and existing host FX/routing regressions pass.
Fresh extended SYNTH device regression is pending. Layout/geometry/render tests
pass; human touchscreen usability and actual listening are not claimed.

## 18. Host tests and CI

The complete inherited workflow plus new capacity/fill and B/C trace tests passes
locally: **92/92 commands**. The final capacity/fill matrix additionally compares
all interpolation modes in forward/reverse at rounding and region boundaries.
Previous all-65,536-phase, old Nearest bit-exactness, exact Linear32/64,
ownership/retirement, all locks, Repeat, Chain, V2 and allocation checks remain.
Linux CI retains ASan/UBSan and runs the new cache matrix with both sanitizers.
ASan/UBSan were not executed on this Windows host. Remote Actions status is
pending; local green checks cannot qualify a real-device candidate.

## 19. Final production configuration

```
P4SDM_INTERPOLATION=0
P4SDM_PCM_READ_CACHE=0
P4SDM_LINEAR_32BIT=0
```

Experimental settings remain opt-in. Production firmware is restored after the
device suite and again after cache-matrix captures. No listening evidence is
available to promote Linear even if a subset passes its timing limit.

## 20. Acceptance answers and remaining limitations

1. **Clock/configuration: yes, 200 MHz is selected and verified through live
   source/divider diagnostics for this v1.3 SDK.** Unrestricted long-term stability,
   exact memory part number and electrical clock measurement are not established.
2. **Locality is strongly supported as the dominant uncached memory bottleneck.**
   Identical-content staggering/order controls support this; direct miss counters
   and a complete post-cache DSP attribution remain absent.
3. **Production cache enablement: pending.** Complete matched A/B/C qualification,
   physical WAV regression and extended monitoring are not all present.
4. **Universal Linear32 replacement: no tested candidate qualifies.** The best
   tested B maximum is 4,949 us (14.75% headroom), still 305.009 us over the reserve.
   Complete A equivalence, all production regressions and listening are also
   required before promotion.

The exact remaining bottleneck cannot yet be assigned to a specific DSP substage.
Hardware rounding divides, duplicate tap validation and full-gain division are
verified costs with independent measured improvements, but removing any one
does not solve the remaining budget. Broader filter/pan/Delay attribution and
paired profiler-overhead controls remain needed.
The old 283 us shortfall is historical and must not be transferred to these new
accepted-event fixtures. Follow-up work remains M21.2; do not start M22.
