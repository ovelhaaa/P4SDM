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
