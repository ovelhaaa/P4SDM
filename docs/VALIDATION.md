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
