# Validação de build — 2026-10-05

Ambiente usado: Windows, Python 3.11.7 do runtime PlatformIO,
PlatformIO Core 6.1.18. Plataforma 55.03.36-1; Arduino 3.3.6;
libs ESP-IDF 5.5.0+sha.f56bea3d1f; toolchain RISC-V 14.2.0+20251107.
Headers/libs efetivamente usados são `esp32p4_es`.

```text
platformio run -e guition_audio -e guition_boot
guition_audio SUCCESS
guition_boot  SUCCESS
```

| Variante | RAM estática | Flash (size tool) | firmware.bin |
|---|---:|---:|---:|
| guition_audio | 26.588 bytes | 364.054 bytes | 376.720 bytes |
| guition_boot | 22.296 bytes | 323.906 bytes | 336.112 bytes |

RAM estática não inclui stack/tasks, heap, DMA dinâmico ou PSRAM; o relatório
Serial registra memória real depois do boot e audio init.

Build gerou warnings dentro do framework fixado (`periph_ctrl.h` e
`esp32-hal-spi.c`), sem erros no código novo. O firmware não inicializa SPI.

Teste host:

```text
g++ -std=c++17 -Wall -Wextra -Werror tests/pcm_mono_test.cpp -o .pio/pcm_mono_test.exe
.pio/pcm_mono_test.exe
exit code 0
```

`git diff --check` passou. Inicialmente só existiam COM6/COM7 Bluetooth.
Posteriormente a placa foi conectada e o diagnóstico foi gravado com hashes
verificados. ESP32-P4 rev. v1.3 e flash 16 MB confirmados pelo esptool.

## Bring-up na unidade conectada

- USB-OTG do ROM: COM12, VID:PID 303A:0012. Gravação direta verificou hashes,
  mas a desconexão durante reset produziu erro de porta no host.
- USB/JTAG: COM13, VID:PID 303A:1001. Upload PlatformIO concluído com SUCCESS.
  Mantido `ARDUINO_USB_MODE=1` para Serial nessa porta.
- Captura Serial confirmou PSRAM física 32 MiB; heap total 33.554.432,
  livre no boot 33.551.856, maior bloco 33.030.132 bytes.
- ES8311 respondeu em 0x18; I2S inicializou 44100/16, playback configurado,
  PA11 habilitado, geração de tom terminou e PA foi desligado sem erro.
- Framework rejeitou CPU 400 MHz e informou suporte a 360 MHz; board JSON
  corrigido para 360 MHz.
- Terminal precisou de `PYTHONIOENCODING=utf-8` para a barra de progresso.

O log da última execução está em `docs/GUITION_SERIAL.log`. Frequências I2S
são valores configurados/reportados, não medidas com osciloscópio.
Áudio audível, pops e underruns do engine ainda dependem de validação física.

## Arquivos desta entrega

Modificado: `README.md` (instruções Guition antes da documentação original).

Criados:

- `.gitignore`
- `platformio.ini`
- `boards/jc4880p4.json`
- `boards/LICENSE`
- `src/audio_diagnostic.cpp`
- `src/hal/guition_board.h`
- `src/hal/guition_board.cpp`
- `src/hal/audio_hal.h`
- `src/hal/audio_hal.cpp`
- `src/hal/pcm_mono.h`
- `lib/es8311/library.json`
- `lib/es8311/PROVENANCE.md`
- `lib/es8311/LICENSE`
- `lib/es8311/src/es8311.h`
- `lib/es8311/src/es8311.c`
- `lib/es8311/src/es8311_reg.h`
- `tests/pcm_mono_test.cpp`
- `docs/GUITION_AUDIT.md`
- `docs/VALIDATION.md`

Os .ino e headers originais de engine/UI/sequencer não foram modificados.
Essa afirmação se refere à entrega inicial de milestones 1–2.
Artefatos de build ficam em `.pio/build/guition_audio` e
`.pio/build/guition_boot`, ignorados pelo Git. Faça upload com PlatformIO,
que envia também bootloader/partições nos offsets corretos.

## Milestone 3 — synth original, sem interface

`guition_synth` compilado e gravado na COM13 com hashes verificados.
RAM estática: 34.084 bytes; flash: 471.142 bytes; firmware.bin: 484.576 bytes.
Usuário informou speaker ainda desconectado e autorizou seguir; não há
confirmação auditiva.

Log real em `docs/GUITION_SYNTH_SERIAL.log`:

```text
[SYNTH] blocks=1723 notes=20 nonzero_mono=421015 peaks L=605 R=606 mono=304
[SYNTH] max_render_us=1756 block_budget_us=5804 render_overruns=0
[SYNTH] write=ESP_OK shutdown=ESP_OK result=PASS (PCM/timing only)
```

Usa loop PCM original e todas as vozes configuradas como synth; não carrega
samples nem habilita FX. Mede somente render, sem varredura de picos/write.
Zero overruns de render não significa zero underruns medidos no DMA.
Os buffers e estado são preparados antes da criação da task.

Arquivos modificados nesta etapa: `DRUM_2026_VSAMPLER_TAB5_2002.ino`
(guards headless e includes FreeRTOS), `synthESP32.ino` (separação de render
e transporte e criação da task delegada no headless), `platformio.ini`,
`README.md`, este relatório. Criados `src/synth_diagnostic.cpp`,
`src/engine/synth_api.h`, `docs/GUITION_SYNTH_SERIAL.log`.
`fx.h`, filtros, tabelas e sequencer permanecem intactos.

## M3.1 — overlapping dry engine qualification (2026-10-05)

Measured on the connected Guition JC4880P443C_I_W, ESP32-P4 at 360 MHz,
32 MiB PSRAM. Raw serial capture: [GUITION_M31_SERIAL.log](GUITION_M31_SERIAL.log).
Upload on COM13 succeeded with all image hashes verified. No audible test was
performed; the speaker may still be disconnected.

The original mixer/DSP -> `render_buffer()` -> audio HAL -> ES8311 path is
unchanged. Each stage resets all voices, then triggers voices 0..N-1 before
rendering any sample. MIDI notes 48..63, original wavetables 0..15 and pans
-120..120 are deterministic and distinct. Original envelope table 3 and length
127 provide a five-second envelope, nonzero over the 517-block / 3.001 s window.
Table 0 would go silent at half its nominal duration and is not suitable here.
Every block checks active voices (pitch != 255 AND amplitude != 0) before and
after rendering; min=max=N for each stage. No retriggers or DSP shortcuts.
All FX remain disabled. No SD, display, touch, USB MIDI or sequencer work.

Timing uses `esp_timer_get_time()` with microsecond resolution. Each stage
records every block, including its first block; percentiles are nearest-rank
on all 517 measurements. Fixed static storage avoids runtime allocation.
Sorting and paced USB serial reports run afterward at task priority 1, after
transport shutdown. Native HWCDC bulk reports initially lost bytes; reporting
now uses a larger TX buffer and paced 32-byte writes, without flush/discard.
No logging or waits are inserted in the realtime measurement loop.

Budget = 256 frames / 44100 Hz = **5804.989 us**. Render time encloses only
`render_buffer()`. Write time includes HAL downmix and blocking I2S output.
Cycle time includes render, active/PCM probes and write, ending before timing
statistics bookkeeping; trigger/stage setup and bookkeeping are outside this
interval. PCM probes add conservative overhead to the measured cycle.

| Voices | Interval | Min us | Average us | p50 us | p95 us | p99 us | Max us |
|---:|---|---:|---:|---:|---:|---:|---:|
| 1 | render | 1625 | 1631.58 | 1633 | 1634 | 1634 | 1729 |
| 1 | write | 22 | 4096.30 | 4133 | 4140 | 4145 | 4147 |
| 1 | cycle | 1685 | 5765.41 | 5804 | 5805 | 5809 | 5812 |
| 4 | render | 1862 | 1875.12 | 1876 | 1876 | 1876 | 1981 |
| 4 | write | 3780 | 3891.17 | 3891 | 3897 | 3897 | 3905 |
| 4 | cycle | 5798 | 5804.02 | 5804 | 5805 | 5810 | 5810 |
| 8 | render | 2154 | 2184.37 | 2183 | 2190 | 2190 | 2335 |
| 8 | write | 3427 | 3581.56 | 3583 | 3584 | 3589 | 3612 |
| 8 | cycle | 5796 | 5804.02 | 5804 | 5805 | 5809 | 5811 |
| 16 | render | 2731 | 2793.07 | 2794 | 2795 | 2795 | 3102 |
| 16 | write | 2658 | 2972.68 | 2972 | 2979 | 2979 | 3039 |
| 16 | cycle | 5796 | 5804.02 | 5804 | 5805 | 5809 | 5812 |

| Voices | Blocks | Active min/max | Render misses | Cycle misses | Write errors | Timeouts |
|---:|---:|---|---:|---:|---:|---:|
| 1 | 517 | 1/1 | 0 | 71 | 0 | 0 |
| 4 | 517 | 4/4 | 0 | 58 | 0 | 0 |
| 8 | 517 | 8/8 | 0 | 111 | 0 | 0 |
| 16 | 517 | 16/16 | 0 | 101 | 0 | 0 |

Cycle misses are comparisons against the exact block budget, with no tolerance.
Blocking writes pace production at approximately one block period; integer-us
clock resolution, scheduling and DMA pacing account for small measured
overshoots (largest 7.011 us). These counts are reported, not suppressed, and
are not a reliable measure of DMA starvation. PASS requires all stage blocks,
correct overlap, nonzero PCM, no silent blocks/rail hits, zero render misses,
zero write errors, successful silence drain and successful shutdown. Cycle
misses are reported independently and do not determine this dry-render PASS.
No zero-cycle-miss or zero-underrun claim is made.

| Voices | Peak L | Peak R | Peak mono | Nonzero mono frames | Silent blocks | Rail frames |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 596 | 38 | 317 | 132094 | 0 | 0 |
| 4 | 2373 | 595 | 1484 | 132254 | 0 | 0 |
| 8 | 4325 | 2163 | 3232 | 132284 | 0 | 0 |
| 16 | 5146 | 4994 | 4923 | 132285 | 0 | 0 |

Mono uses the exact current HAL arithmetic `(int32_t(L) + R) / 2`; a nonzero
frame means that mono sample is nonzero. A fully silent block fails the test.
Rail frames count frames where L, R or mono equals -32768 or 32767. This is
an output sanity check, not a pre-soft-clip excursion counter: the original
`soft_clip()` and upstream integer arithmetic are untouched, so absence of
rail hits does not prove absence of internal overflow or nonlinear clipping.

Memory (bytes), comparing the same allocated task and live transport:

| Snapshot | PSRAM free | PSRAM largest block | Internal free |
|---|---:|---:|---:|
| Before stress, task allocated | 33550588 | 33030132 | 520924 |
| After stress, before shutdown | 33550588 | 33030132 | 520924 |
| After transport shutdown, task still allocated | 33550636 | 33030132 | 526204 |

No heap loss across stress. Timing storage raises static RAM to 59124 bytes;
final synth flash size is 473434 bytes (size tool). Stack/heap/DMA are additional.

Transport validation: ES8311 responds at 0x18, codec playback setup and I2S
44100/16 configuration succeed, all 2068 measured writes and silence drain
succeed, shutdown succeeds. This validates the transport API path and codec
register access; physical I2S waveforms, actual clock rate, audible sound and
pops were not measured. The pinned IDF I2S header exposes TX `on_sent` and
`on_send_q_ovf` callbacks, neither a reliable playback underrun/starvation
metric. No invented underrun counter is reported.

Resonance: `LowPassFilter::setResonance(uint8_t)` explicitly documents 0..255,
255 most resonant, and assigns the argument directly to `q`. The old constant
511 converted modulo 256 to 255. It is now explicitly `uint8_t(255)`, preserving
original audible behavior and removing the conversion warning. Filter math,
Tab5 conditional implementation and all other original DSP remain unchanged.

Build/CI: local `guition_boot`, `guition_audio`, `guition_synth` builds succeeded
with PlatformIO Core 6.1.18, Python 3.11.7, pinned pioarduino 55.03.36-1,
Arduino 3.3.6 and ESP-IDF 5.5.0 libs (`esp32p4_es`). Final synth was rebuilt and
flashed after reporting fixes. Host `pcm_mono_test` compiled with
`g++ -std=c++17 -Wall -Wextra -Werror` and exited 0. `git diff --check` passed.
Framework periph_ctrl/SPI warnings remain; resonance warning is gone.
`.github/workflows/guition.yml` builds all three diagnostics and runs that host
test without hardware, pinning Core/Python and the platform URL. GitHub's
hosted workflow has not been run in this local session. No Tab5 hardware build
or hardware run was performed.

Recommendation: **dry render engine qualified for this M3.1 deterministic
workload**, with 16-voice p99 2795 us and max 3102 us (53.4% of block budget,
2702.989 us observed render headroom). This is not worst-case qualification
across all 44 wavetables, filter/modulation settings, competing tasks, hours of
operation or FX. DMA starvation remains unmeasured and small blocking-cycle
overshoots remain visible. FX needs its own incremental budget/transport
qualification; audible validation is still pending. M3.1 stops here; no next
milestone is enabled automatically.

## M3.1 hosted CI follow-up

The first hosted Guition workflow for `fed1b61442657f411429ced52fb85866f691bf5b`
completed successfully: [GitHub Actions run 37395506007](https://github.com/ovelhaaa/P4SDM/actions/runs/37395506007).
This supersedes the historical M3.1 session's statement that hosted CI had not
yet been run. The M3.1 hardware tables and serial log above are preserved.

## M3.2 original FX qualification — 2026-10-05

The Guition target is `guition_fx`, extending the existing headless diagnostic.
It calls the same original `render_buffer()` with the original synth, mixer,
eight FX and parallel send/return routing, followed by the existing audio HAL
and ES8311. The requested combinations are groups of enabled parallel effects;
the original firmware does not cascade Chorus into Delay into Reverb. Changing
that routing would change the sound and is outside this milestone.

### Allocation audit and fixes

All lengths, presets, coefficients and per-sample DSP formulas are unchanged.
Reverb replaces `std::vector<float>` with noncopyable checked float storage;
its 24 allocations retain exact lengths, zero initialization and indexing.
Every allocation prefers `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`. Guition headless
builds require PSRAM and report failure rather than consume internal heap.
Tab5 retains the original ordinary `calloc` fallback when PSRAM allocation
fails. Ordinary heap placement on Tab5 depends on its allocator configuration.
No Tab5 build or hardware validation was performed in M3.2.

| FX | Persistent sample buffers | Payload bytes | Original allocation/failure behavior | Current initialization/failure behavior |
|---|---|---:|---|---|
| Reverb | 8 comb + 4 allpass per channel, float32 | 407200 | `vector.resize`, ordinary allocator, PSRAM not guaranteed; failure unchecked and process unsafe before init | 24 checked PSRAM allocations; partial failure frees all; `bool init`, readiness query, dry bypass when unready |
| Delay | 2 × 88200 int16 | 352800 | PSRAM calloc then ordinary fallback; ready checked, failed process returns zero; partial/repeated init leaked | Checked pair; partial/repeated init cleaned up; explicit failure; original zero return preserved; invalid delay read guarded |
| Chorus | 2 × 4096 int16 | 16384 | PSRAM calloc then fallback; unready process dry bypass; partial/repeated init leaked | Checked pair with cleanup, explicit failure and dry bypass |
| Flanger | 2 × 4096 int16 | 16384 | Same as Chorus | Checked pair with cleanup, explicit failure and dry bypass |
| Tremolo | No heap sample buffer; scalar LFO state | 0 | Scalar init always marked ready | Finite positive sample rate checked; bool/readiness; unready dry bypass |
| Ring Mod | No heap sample buffer; scalar oscillator state | 0 | Scalar init always marked ready | Finite positive sample rate checked; bool/readiness; unready dry bypass |
| Distortion | No heap sample buffer; two scalar filter histories | 0 | Scalar init always marked ready | Finite positive sample rate checked; bool/readiness; unready dry bypass |
| Bitcrusher | No heap sample buffer; scalar counter and stereo held samples | 0 | Scalar init always marked ready | Finite positive sample rate checked; bool/readiness; unready dry bypass |

Reverb left comb lengths are 4464, 4752, 5108, 5424, 5688, 5964, 6228,
6468; left allpass lengths are 900, 1364, 1764, 2224. Every right buffer adds
92 samples. Total is 101800 floats. Total FX buffer payload is **792768 bytes**.
Global objects, parameters and scalar histories use static RAM separately.
Measured heap consumption includes allocator alignment/metadata and therefore
exceeds buffer payload. Each individually initialized FX is released and both
heaps must return exactly to their pre-init free-byte counts. All required FX
are then initialized together before audio begins.

`init()` remains control/startup work: it allocates and/or logs and must never
run in the realtime task. `reset()` clears existing buffers/history without
allocation, but clearing large buffers is also kept outside realtime timing.
The diagnostic performs all allocations before creating the measurement task.
No allocation, logging or sorting occurs in `render_buffer()` or the measured
loop. Host tests inject each of the 24 possible reverb allocation failures and
each stereo-buffer failure, check cleanup and unready processing, repeated
initialization, invalid sample rates and absence of allocations during DSP.

### Workload and exact parameters

Every stage runs 517 blocks × 256 frames = 132352 frames / 3.001179 seconds of
audio at 44100 Hz, CPU 360 MHz. All 16 voices trigger before the first sample;
MIDI 48..63, distinct wavetable indices 0..15, pan -120..120 in steps of 16,
envelope table 3, length 127 (5 seconds), modulation 64, voice volume 80,
single oscillator and zero detune, master volume 60, filters at original
setting 0. Active voice checks run before and after each block. Oscillator,
envelope, filter and FX histories restart for each M3.2 stage. The original
Tab5 synth trigger and filter processing are unchanged; the diagnostic calls
an explicit filter-history reset outside measurement.

Dry has all FX disabled and all send masks zero, matching M3.1. Every enabled
FX receives all 16 voices (`0xffff`); disabled sends are zero. Every FX return
uses original level 100/256. The same settings are used individually and in
combinations, including full processing paths:

| FX | Preset | Exact original parameters |
|---|---|---|
| Reverb | 8 Deep Space | room=.93, damping=.10, wet=.80, dry=.50, width=1 |
| Delay | Manual original defaults | length=88200, time=12000 samples (272.109 ms), feedback=120/256, input=160/256 |
| Chorus | 1 Juno I | speed=.50 Hz, depth=180 samples, mix=.50, base=600 samples, feedback=0 |
| Flanger | 1 Classic Jet | rate=.20 Hz, depth=3 ms, manual=2 ms, feedback=.60, mix=.50 |
| Tremolo | 5 Ping Pong | rate=3 Hz, depth=1, shape=0, spread=1 (both channel LFOs evaluated) |
| Ring Mod | 2 Deep Growl | frequency=50 Hz, mix=.80, shape=.20, drive=1.20 |
| Distortion | 2 Blues Driver | drive=8, tone=.70, level=.80, mix=1 |
| Bitcrusher | 2 8-Bit Console | bits=8, rate divisor=1 (crush every sample), drive=1, mix=1 |

A enables Chorus + Delay + Reverb; B enables Chorus + Flanger + Tremolo;
C enables Distortion + Bitcrusher + Delay + Reverb; D enables all eight FX.
There is no adaptive disabling or quality reduction on deadline misses.

### Qualification rule and instrumentation

Budget is **5804.988662 us**. `FAIL` means any measured render deadline miss;
`INIT_FAIL` means required buffers could not initialize safely. With no misses,
`SAFE` requires the worst observed render to consume at most 80% of budget,
reserving at least 1160.998 us (20%) for future work; otherwise it is `TIGHT`.
This is a documented engineering reserve, not a guarantee that future SD/UI
tasks fit. Classification uses worst observed time including the first block,
not only p99. PCM/voice/write validity is reported separately and must also be
checked before calling a measured stage qualified.

Render timing encloses only `render_buffer()` with `esp_timer_get_time()`.
Nearest-rank p50/p95/p99 include all blocks, with min/mean/max and ordered raw
times retained in the real serial log. Fixed static arrays hold measurements;
PCM/voice probes and write timing sit outside render timing. Sorting and
paced serial reports occur at priority 1 after audio shutdown. Blocking write
and cycle timings remain in the log; DMA pacing crossings do not count as DSP
misses. Silence drain and shutdown are checked. Failed DSP stages are retained
and later stages proceed when transport remains usable.

The safety changes preserve deterministic output: `tests/compare_fx_baseline.py`
compiles the actual `fx.h` from `fed1b614` and the current version, then compares
all eight output digests across 132352 deterministic stereo frames per FX.
All eight matched locally, covering buffer wraparound. This is source/DSP
regression evidence, not audible validation or a proof for every possible preset.

Build and capture:

```text
pio run -e guition_boot -e guition_audio -e guition_synth -e guition_fx
pio run -e guition_fx -t upload --upload-port COM13
python tests/capture_guition.py --port COM13 --output docs/GUITION_M32_SERIAL.log --seconds 150
python tests/analyze_m32.py docs/GUITION_M32_SERIAL.log
python tests/compare_fx_baseline.py
```

The Windows session uses `C:\.platformio\penv\Scripts\python.exe -m platformio`,
Core 6.1.18 / Python 3.11.7 and the existing pinned toolchain. Local boot/audio/
synth builds passed. Framework periph_ctrl/SPI warnings remain unchanged.
The workflow now builds all four diagnostics and runs PCM, FX safety and
original/current output regression tests.

Hosted CI passed for `ee9705c` ([run 37397634093](https://github.com/ovelhaaa/P4SDM/actions/runs/37397634093))
and the final hardware-qualified source `f189ec8`
([run 37397973210](https://github.com/ovelhaaa/P4SDM/actions/runs/37397973210)).
The latter successfully built all four environments and passed all three host
checks, including the original/current FX output comparison on Linux.


### Final hardware results

Hardware-qualified source: `f189ec8` (following `ee9705c`), Guition ESP32-P4 revision v1.3, USB/JTAG COM13. Flash hashes verified.
Firmware SHA256: `09ecd03ae6f238dfc217e6d8a080cac369edfb893c17562cb80e99fdf5fa1897`.
Local FX build uses 106344 bytes static RAM and 479154 bytes flash (PlatformIO size report).
Real capture: [GUITION_M32_SERIAL.log](GUITION_M32_SERIAL.log). No M3.1 log was overwritten.

#### Individual FX performance

| Stage | p99 us | Max us | Delta p99/max vs dry us | Budget p99/max % | Headroom p99/worst us | Render misses | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Dry | 2774 | 3511 | 0 / 0 | 47.8 / 60.5 | 3030.989 / 2293.989 | 0 | SAFE |
| Reverb | 3684 | 4265 | 910 / 754 | 63.5 / 73.5 | 2120.989 / 1539.989 | 0 | SAFE |
| Delay | 2898 | 3592 | 124 / 81 | 49.9 / 61.9 | 2906.989 / 2212.989 | 0 | SAFE |
| Chorus | 3195 | 3824 | 421 / 313 | 55.0 / 65.9 | 2609.989 / 1980.989 | 0 | SAFE |
| Flanger | 3189 | 3833 | 415 / 322 | 54.9 / 66.0 | 2615.989 / 1971.989 | 0 | SAFE |
| Tremolo | 3257 | 3917 | 483 / 406 | 56.1 / 67.5 | 2547.989 / 1887.989 | 0 | SAFE |
| RingMod | 3116 | 3795 | 342 / 284 | 53.7 / 65.4 | 2688.989 / 2009.989 | 0 | SAFE |
| Distortion | 2952 | 3651 | 178 / 140 | 50.9 / 62.9 | 2852.989 / 2153.989 | 0 | SAFE |
| Bitcrusher | 3363 | 4157 | 589 / 646 | 57.9 / 71.6 | 2441.989 / 1647.989 | 0 | SAFE |

#### Combination performance

| Stage | p99 us | Max us | Delta p99/max vs dry us | Budget p99/max % | Headroom p99/worst us | Render misses | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | 4243 | 4755 | 1469 / 1244 | 73.1 / 81.9 | 1561.989 / 1049.989 | 0 | TIGHT |
| B | 4081 | 4606 | 1307 / 1095 | 70.3 / 79.3 | 1723.989 / 1198.989 | 0 | SAFE |
| C | 4595 | 5260 | 1821 / 1749 | 79.2 / 90.6 | 1209.989 / 544.989 | 0 | TIGHT |
| D | 6278 | 6676 | 3504 / 3165 | 108.1 / 115.0 | -473.011 / -871.011 | 517 | FAIL |

#### Complete render distribution

| Stage | Min us | Average us | p50 us | p95 us | p99 us | Max us |
| --- | --- | --- | --- | --- | --- | --- |
| Dry | 2717 | 2772.63 | 2773 | 2774 | 2774 | 3511 |
| Reverb | 3526 | 3626.06 | 3625 | 3665 | 3684 | 4265 |
| Delay | 2833 | 2888.77 | 2887 | 2892 | 2898 | 3592 |
| Chorus | 3029 | 3152.07 | 3172 | 3189 | 3195 | 3824 |
| Flanger | 3035 | 3148.91 | 3146 | 3186 | 3189 | 3833 |
| Tremolo | 3115 | 3214.38 | 3219 | 3251 | 3257 | 3917 |
| RingMod | 3045 | 3090.66 | 3094 | 3111 | 3116 | 3795 |
| Distortion | 2895 | 2948.14 | 2947 | 2951 | 2952 | 3651 |
| Bitcrusher | 3304 | 3358.16 | 3356 | 3362 | 3363 | 4157 |
| A | 3991 | 4169.80 | 4178 | 4225 | 4243 | 4755 |
| B | 3739 | 3977.43 | 3998 | 4057 | 4081 | 4606 |
| C | 4438 | 4538.58 | 4537 | 4581 | 4595 | 5260 |
| D | 5853 | 6131.48 | 6145 | 6240 | 6278 | 6676 |

#### PCM and transport sanity

| Stage | Blocks | Active min/max | Peak L/R/mono | Nonzero mono frames | Silent blocks | Rail frames | Write failures/timeouts | Cycle crossings |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Dry | 517 | 16/16 | 4651/5594/4734 | 132293 | 0 | 0 | 0/0 | 116 |
| Reverb | 517 | 16/16 | 5948/6861/5880 | 132293 | 0 | 0 | 0/0 | 157 |
| Delay | 517 | 16/16 | 4725/6074/5133 | 132278 | 0 | 0 | 0/0 | 103 |
| Chorus | 517 | 16/16 | 5831/6721/5690 | 132304 | 0 | 0 | 0/0 | 65 |
| Flanger | 517 | 16/16 | 6272/6752/5637 | 132293 | 0 | 0 | 0/0 | 108 |
| Tremolo | 517 | 16/16 | 6029/7435/5774 | 132294 | 0 | 0 | 0/0 | 56 |
| RingMod | 517 | 16/16 | 6590/6992/6203 | 132292 | 0 | 0 | 0/0 | 102 |
| Distortion | 517 | 16/16 | 6235/7177/6318 | 132321 | 0 | 0 | 0/0 | 65 |
| Bitcrusher | 517 | 16/16 | 6455/7773/6580 | 132308 | 0 | 0 | 0/0 | 102 |
| A | 517 | 16/16 | 7100/8466/7072 | 132305 | 0 | 0 | 0/0 | 172 |
| B | 517 | 16/16 | 8704/9727/7500 | 132316 | 0 | 0 | 0/0 | 71 |
| C | 517 | 16/16 | 9407/11102/9708 | 132325 | 0 | 0 | 0/0 | 172 |
| D | 517 | 16/16 | 13357/15677/13079 | 132336 | 0 | 0 | 0/0 | 517 |

#### Ordered timing and spike inspection

| Stage | First block us | Top 3 (block:us) | Quarter means us |
| --- | --- | --- | --- |
| Dry | 3511 | 0:3511, 28:2775, 191:2775 | 2776.3 / 2771.4 / 2771.4 / 2771.4 |
| Reverb | 4265 | 0:4265, 448:3695, 208:3691 | 3626.2 / 3629.2 / 3621.3 / 3627.6 |
| Delay | 3592 | 0:3592, 4:2901, 16:2901 | 2892.8 / 2887.8 / 2886.9 / 2887.6 |
| Chorus | 3824 | 0:3824, 224:3198, 240:3198 | 3124.6 / 3183.2 / 3147.1 / 3153.3 |
| Flanger | 3833 | 0:3833, 323:3196, 384:3191 | 3106.1 / 3144.5 / 3163.8 / 3181.0 |
| Tremolo | 3917 | 0:3917, 52:3257, 108:3257 | 3216.4 / 3214.9 / 3210.8 / 3215.4 |
| RingMod | 3795 | 0:3795, 191:3117, 319:3117 | 3094.2 / 3089.7 / 3089.2 / 3089.6 |
| Distortion | 3651 | 0:3651, 113:2953, 270:2953 | 2951.7 / 2946.8 / 2946.9 / 2947.2 |
| Bitcrusher | 4157 | 0:4157, 4:3366, 394:3364 | 3362.1 / 3356.5 / 3357.4 / 3356.7 |
| A | 4755 | 0:4755, 188:4267, 176:4262 | 4144.1 / 4201.4 / 4156.9 / 4176.7 |
| B | 4606 | 0:4606, 338:4090, 513:4085 | 3898.2 / 4009.6 / 3988.8 / 4012.8 |
| C | 5260 | 0:5260, 188:4624, 67:4623 | 4537.9 / 4542.2 / 4533.7 / 4540.5 |
| D | 6676 | 0:6676, 343:6310, 333:6289 | 6049.1 / 6164.9 / 6136.0 / 6175.5 |

#### Memory snapshots

| Snapshot | PSRAM free bytes | PSRAM largest bytes | Internal free bytes |
| --- | --- | --- | --- |
| boot | 33551856 | 33030132 | 479164 |
| engine init (FX disabled) | 33551856 | 33030132 | 479164 |
| before FX allocation | 33551856 | 33030132 | 479164 |
| Reverb | 33139600 | 33030132 | 479164 |
| Delay | 33191400 | 33030132 | 479164 |
| Chorus | 33534952 | 33030132 | 479164 |
| Flanger | 33534952 | 33030132 | 479164 |
| Tremolo | 33551856 | 33030132 | 479164 |
| RingMod | 33551856 | 33030132 | 479164 |
| Distortion | 33551856 | 33030132 | 479164 |
| Bitcrusher | 33551856 | 33030132 | 479164 |
| all FX initialized | 32745336 | 32505844 | 479164 |
| before stress (transport ready) | 32744068 | 32505844 | 473384 |
| before stress (task allocated) | 32744068 | 32505844 | 464816 |
| after stress (before transport shutdown) | 32744068 | 32505844 | 464816 |
| after shutdown | 32744116 | 32505844 | 470096 |

All 13 stages and 6721 ordered block timings validated; heap restored after all eight individual initializations; no stress heap loss.

All 13 stages complete 517 blocks with active min=max=16. All 6721
measured writes, silence drain and transport shutdown succeed, with zero
timeouts, silent blocks or output rail hits. D's overall firmware `result=FAIL`
is expected and retained: all 517 of its render blocks miss the deadline.
Working transport and sane PCM do not make D realtime-safe. Actual DMA playback
starvation remains unmeasured; the I2S callbacks do not provide a trustworthy
underrun counter. Full write/cycle percentiles remain in the raw log.

The new dry mean is 2772.63 us versus M3.1's 2793.07 us (-20.44 us / -0.73%);
p99 is 2774 versus 2795 us (-21 us / -0.75%). Maximum is 3511 versus 3102 us
(+409 us / +13.18%), entirely the first block in M3.2. M3.2 dry is the first
stage after all FX allocation/reset work, whereas M3.1's 16-voice stage followed
the 1/4/8-voice stages; M3.2 also explicitly restarts oscillator/filter history.
The changed first-block maximum must not be hidden or attributed to an FX.

Every stage's largest render time is block 0. No FX allocates or initializes in
that interval, and all recorder writes, PCM probes, sorting and logging are
outside it. Common first-block excess, including dry, points to stage-start
code/data cache or scheduling effects rather than an effect-specific lazy
allocation. This is an inference: no cache/interrupt trace was captured, so
exact attribution between DSP cache stalls and task/interrupt scheduling is
unresolved. These times remain included in max, headroom and classification.

Later reverb timings remain in a narrow band: top non-first blocks are 3695
and 3691 us; quarter means stay near 3621..3629 us. The data do not show an
escalating pathological floating-point tail. Chorus and Flanger show smooth
state-dependent variation, and Flanger's quarter means rise 3106..3181 us
during the partial LFO sweep; B/D inherit modulation-dependent variation.
Bitcrusher's p99 increment is 589 us despite zero delay storage; its selected
rate divisor 1 runs the original crush path every sample. Reverb is the largest
individual p99 increment (910 us). Neither algorithm is rewritten.

The ordered samples cover repeated reverb wraps (longest buffer 6560 samples),
modulation-buffer wraps (4096 samples) and a delay write-buffer wrap at frame
88200 (inside block 344). Reverb's highest later blocks (448/208), Chorus's
(224/240), Flanger's (323/384), and Delay's (4/16) do not align with a common
buffer-wrap deadline spike. D's later maximum at block 343 is close to the
delay wrap, but occurs in a band of modulation-dependent high values and is
below the first-block maximum; this run cannot establish a wrap-related cause.
No new isolated late deadline failure appears in the individually passing FX.
LFO/state timing patterns are observable, but no multi-run periodicity or
denormal-specific CPU trace was collected.

#### Allocation consumption and heap interpretation

| FX | Payload bytes | Measured PSRAM heap bytes individually | Internal heap bytes |
|---|---:|---:|---:|
| Reverb | 407200 | 412256 | 0 |
| Delay | 352800 | 360456 | 0 |
| Chorus | 16384 | 16904 | 0 |
| Flanger | 16384 | 16904 | 0 |
| Tremolo | 0 | 0 | 0 |
| Ring Mod | 0 | 0 | 0 |
| Distortion | 0 | 0 | 0 |
| Bitcrusher | 0 | 0 | 0 |
| All FX | 792768 | 806520 | 0 |

All eight individual release checks restore both heaps exactly. Before/after
stress with the same task and live transport, PSRAM free stays 32744068 bytes
and internal free stays 464816 bytes; largest PSRAM block stays 32505844 bytes.
There is **zero unexpected heap loss** across the qualification run.
FX buffers remain allocated afterward. Transport shutdown returns 48 PSRAM
bytes and 5280 internal bytes; shared I2C bus lifetime remains outside audio
shutdown, so boot-versus-shutdown is not the leak comparison. Static timing
arrays and task stack are qualification infrastructure, separate from FX heap.

#### Qualification outcome, limitations and next assembly recommendation

All eight individual FX are SAFE for this exact headless workload. B is SAFE,
with 1198.989 us (20.65%) worst observed headroom, only 37.991 us above the
documented reserve boundary. A is TIGHT (1049.989 us / 18.09% worst headroom),
C is TIGHT (544.989 us / 9.39%), and D FAILS (-871.011 us worst headroom).
SAFE is measured headroom, not a claim of universal safe operation.

For initial full-firmware assembly, start with **Delay alone**, which retains
2212.989 us worst observed headroom. If selecting one of the four measured
multi-FX groups, **B (Chorus + Flanger + Tremolo)** is the only SAFE group;
requalify with real competing tasks before adopting it as a default. Defer A/C
until a combined-task budget is measured. Do not enable D at 16 voices with
these settings. This milestone does not automatically enable any chain.

Only 3.001 seconds per stage, one board/run and these presets/16 wavetables
are qualified. Reverb's long tail is not a steady-state or denormal torture
test; Flanger's 0.2 Hz LFO has a 5-second period, longer than the window.
Other presets, all 44 wavetables, longer sessions, resonance/modulation extremes
and PSRAM contention with future tasks remain unqualified. The largest risks
are combined FX cost, cold-stage outliers, small multi-FX reserve, future
task/cache/PSRAM contention and unmeasured DMA starvation. No DSP quality,
buffer length, coefficient or preset was reduced to obtain a category.

Audible behavior, speaker output, physical I2S waveforms, actual clock rate,
pops and subjective effect quality remain **unverified**. No speaker listening
test was performed. No SD card, display, touch, USB MIDI, Wi-Fi, sequencer UI or
sampler loading was implemented or tested. M3.2 stops here.

### M4 — Guition display/touch and competing UI workload

See [the M4 hardware report](GUITION_M4.md) for the implementation,
buffer/task layout, all six timing distributions, PCM/UI/touch statistics,
memory snapshots and reproduction details. Genuine captures are
`GUITION_M4_DISPLAY_SERIAL.log` and `GUITION_M4_UI_AUDIO_SERIAL.log`;
`tests/analyze_m4.py` validates all 3102 measured audio blocks.

Display and GT911 initialize successfully on the real board, sharing I2C0
with ES8311. The user physically confirmed correct landscape corner markers,
colors/borders and touch tracking for the requested corner/center/drag test.
The appended standalone serial capture records26 presses/26 releases with
zero errors and matching native/logical coordinate transforms. Exact corner
coverage relies on the user's physical confirmation; the combined run has
no human touch packets.

**Combined audio/UI qualification FAILS:** core1 light/heavy updates have
3/181 render deadline misses; the transition into core0 static has two.
Core0 light/heavy individually have zero misses and retain at least20%
worst-render reserve, achieving19.993/8.644 FPS versus30 requested. This
supports core0 low-priority UI as the next candidate, not a complete pass.
Do not adopt core1 rendering for this16-voice plus Delay workload.
Both heaps remain stable; all modes have zero UI/touch/transport API errors,
zero silent blocks and zero rail frames. Audio listening and reliable DMA
starvation counting remain unverified. Original synth and FX are preserved.
All six local builds and hosted CI run37400942633 pass, including the
existing PCM/FX checks and new display geometry test. No SD card is needed.

### M4.1 — native queued display pipeline

See [the complete M4.1 report](GUITION_M41.md) for architecture, source audit,
component timing, all strategies/transitions, memory and genuine raw captures.
The original DSP and hardware initialization are preserved. Three native
buffers replace two native plus one logical surface; dirty repair/cache work
and preparation overlap retain conservative two-refresh retirement.

Final physical interaction run: 10336 blocks with 16 voices plus Delay,
29.474 acknowledged FPS including 12 page constructions, zero render misses,
p99/max4005/4046 us and 30.30% worst reserve. Touch at100.010 Hz records all
corners/center,221 presses/releases and1889 movements with zero API errors;
the user confirms correct picture and tracking without visual faults.
All live heap/largest-block measurements remain unchanged. Short cold LIGHT
is28.323 FPS; constant30 FPS is not guaranteed. The bounded diagnostic trace
retains512 events and truncates302 further entries while aggregate counters
continue. The analyzer checks19125 raw timings and all retained mappings.

This supports beginning the real UI within the measured dirty-rendering and
core0 priority envelope, with skipped visual frames during expensive page
changes. Future UI complexity needs renewed measurement. Audio listening and
reliable DMA starvation counts remain unverified. M4.1 stops here, without SD
or full drum-machine UI. Source commit89bb050 passes all seven hosted builds
and existing host checks; final evidence adds recorded M4.1 checks to CI.
