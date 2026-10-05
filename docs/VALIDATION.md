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

`git diff --check` passou. A lista de portas desta máquina apresentou somente
COM6/COM7 Bluetooth. Nenhum flash foi feito; PSRAM, ACK do codec, clocks reais,
áudio audível, pops e underruns **não foram validados em hardware**.

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
Artefatos de build ficam em `.pio/build/guition_audio` e
`.pio/build/guition_boot`, ignorados pelo Git. Faça upload com PlatformIO,
que envia também bootloader/partições nos offsets corretos.
