# P4SDM — Guition audio bring-up (milestones 1–3)

Fork de `zircothc/M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026` para Guition
JC4880P443C_I_W / JC-ESP32P4-M3. Nesta etapa, o firmware novo é um diagnóstico
de boot/PSRAM e áudio ES8311 → NS4150 → SPEAKER. O engine original foi
preservado; o milestone 3 já oferece um teste do mixer/wavetable original sem
interface. A confirmação auditiva ainda está pendente: o speaker não foi ligado.

A [auditoria](docs/GUITION_AUDIT.md) registra os acoplamentos, inventário gráfico,
riscos e plano mínimo. Nenhum display, SD, touch, MIDI Host ou Wi-Fi é
inicializado pelo diagnóstico.

## Compilar e flashear

Use Python **3.10–3.13** e PlatformIO Core **6.1.18** (versão usada na validação). Python 3.14 é rejeitado
pela plataforma fixada. Use o terminal do PlatformIO com seu próprio runtime
Python, evitando misturar seu `penv` com outro Python de versão diferente.
Os comandos abaixo pressupõem `pio` disponível nesse terminal:

```powershell
# No Windows, evita falha de encoding na barra de progresso do esptool:
$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"
pio run -e guition_boot
pio run -e guition_audio
pio device list
# Troque COMx pela porta USB da Guition (não por uma porta Bluetooth):
pio run -e guition_audio -t upload --upload-port COMx
pio device monitor --port COMx --baud 115200
```

Nesta máquina o executável funcional é
`C:\.platformio\penv\Scripts\python.exe -m platformio` (Python 3.11.7).
Após abrir o monitor, pressione RESET para ver o boot.
Este build usa `ARDUINO_USB_MODE=1`: conecte à porta **USB/JTAG** da placa
(303A:1001; COM13 na unidade testada). A porta USB-OTG do ROM apareceu como
303A:0012/COM12, mas não é a porta Serial desta variante. Se necessário, entre no modo de
download com BOOT/RESET conforme a placa. Não flasheie o `.ino.bin` antigo.

`platformio.ini` fixa pioarduino **55.03.36-1** (Arduino **3.3.6**) e a board
definition usa **esp32p4_es**, flash 16 MB QIO, PSRAM QSPI. A definição vem do
[BSP funcional](https://github.com/ultramcu/guition-jc4880p4-bsp/tree/324970bade0d1f4e52880fe8016580368bc1e06e),
com licença em `boards/LICENSE`; o nome foi corrigido para explicitar ES.
Na unidade rev. 1.3 conectada, o framework rejeitou 400 MHz e informou suporte
a 360 MHz; por isso `f_cpu` foi ajustado para 360 MHz.
Não trocar automaticamente pela plataforma mais recente. Para uma placa de
outra revisão, conferir o chip real antes de alterar o alvo.

## Speaker e pinagem

| Sinal | GPIO |
|---|---:|
| ES8311 I2C SDA / SCL | 7 / 8 |
| I2S MCLK / BCLK / WS | 13 / 12 / 10 |
| P4 DOUT → codec | 9 |
| codec DIN → P4 (não usado) | 48 |
| NS4150 PA enable, HIGH | 11 |

ES8311: endereço I2C **0x18**. Conferidos contra pinos do BSP e
[esquemáticos da placa](https://github.com/ultramcu/guition-jc4880p443c-i-w/tree/main/schematic).
A folha 02 mostra CN1 SPEAKER entre **SPEAKER_P e SPEAKER_N**, saídas em ponte
do NS4150. Conecte um alto-falante passivo entre esses dois terminais;
nenhum deles deve ir ao GND ou ser tratado como saída de fone/linha.

Para o bring-up, use **8 Ω / pelo menos 2 W** como escolha conservadora.
O [datasheet NS4150 da Nsiway (cópia)](https://snapeda.s3.amazonaws.com/datasheets/NS4150_power_amplifier.pdf)
caracteriza cargas 4 Ω e 8 Ω: a 5 V, 1% THD, aproximadamente 2 W/4 Ω e
1,3 W/8 Ω; a potência depende da tensão e distorção. O esquemático alimenta
o amp por VOUT-BAT e não especifica um speaker obrigatório; portanto **não**
se promete “3 W” na alimentação real da placa. A seleção 8 Ω/2 W é uma
recomendação baseada nessas condições, não especificação confirmada do kit.
Comece com o ganho atenuado definido no firmware.

## Diagnóstico e logs esperados

`guition_boot` só informa revisão, núcleos e memória. `guition_audio` exige
32 MiB de PSRAM e toca 440 Hz por cinco segundos; reset repete o teste.
Estas são mensagens esperadas, não logs de uma placa já validada:

```text
[BOOT] P4SDM Guition diagnostic; cores=2 revision=...
[MEM] boot PSRAM total=... free=... largest=... internal_free=...
[BOOT] 32 MiB physical PSRAM detected OK
[AUDIO] I2C init OK
[AUDIO] ES8311 found at 0x18
[AUDIO] I2S init 44100/16 OK, MCLK=11289600 Hz
[AUDIO] ES8311 playback configured (capture serial port disabled)
[AUDIO] PA GPIO11 enabled
[MEM] audio init PSRAM total=... free=... largest=... internal_free=...
[AUDIO] Playing 440 Hz test tone for 5 seconds
[AUDIO] Tone finished; PA disabled. Reset to replay.
```

Erros imprimem nome/valor `esp_err_t`, interrompem o teste e desligam PA11.
O teste de 32 MiB usa o tamanho físico; `total` no relatório é o heap PSRAM
utilizável e pode ser menor por reservas do sistema.
Não há retry infinito. Falha de PSRAM impede inicialização do áudio.
O volume do codec começa em 80 na escala do driver (registro 203/255,
atenuado; **não significa 80% de potência**), seno com pico 8192/32767 e
rampas de 20 ms. I2S recebe silêncio antes e depois do tom para reduzir pops.

## HAL e realtime

### Synth original sem SD (milestone 3)

```powershell
pio run -e guition_synth
pio run -e guition_synth -t upload --upload-port COM13
pio device monitor --port COM13 --baud 115200
```

Na variante `guition_synth`, `P4SDM_HEADLESS=1` exclui inicialização/periféricos
Tab5 do sketch principal. `src/synth_diagnostic.cpp` inclui os globais originais
e `synthESP32.ino` na mesma unidade C++, com declarações em
`src/engine/synth_api.h`. Não há cópia do mixer nem geração de código no build.
Essa organização é uma ponte para a futura separação da aplicação/UI em módulos.

`render_buffer()` contém o loop PCM original; o transporte foi separado dele.
O teste envia `audio::write(out_buf, DMA_BUF_LEN)`. Inicializa todas as vozes
antes de criar a task de áudio. M3.1 mede etapas determinísticas de 1/4/8/16
vozes simultâneas por 3 segundos cada, com notas/wavetables/PAN distintos e
envelope original de 5 segundos (tabela 3). As mudanças de voz pertencem à mesma
task de áudio, evitando escrita concorrente nos globais neste diagnóstico.
Ao final envia silêncio, desliga o PA e imprime métricas em prioridade baixa.

FX ficam **desativados**, sem alocação, neste teste incremental; os algoritmos
em `fx.h` permanecem intactos. Não há SD, sampler carregado, sequencer,
persistência, display, touch ou USB Host nesta variante.

Resultados M3.1 e limitações em [VALIDATION.md](docs/VALIDATION.md), com captura
real em [GUITION_M31_SERIAL.log](docs/GUITION_M31_SERIAL.log).

Na execução **anterior ao M3.1** registrada em [GUITION_SYNTH_SERIAL.log](docs/GUITION_SYNTH_SERIAL.log):
1.723 blocos, 20 notas, 421.015 frames mono não nulos, picos L/R 605/606,
mono 304, render máximo **1.756 µs**, prazo **5.804 µs**, zero overruns de
render, write/shutdown `ESP_OK`. Isso confirma PCM e tempo do teste dry;
não mede underruns de DMA, todos os FX ou latência fim a fim.

O build expõe um warning já existente: `reso=511` é convertido para `uint8_t`
255 pelo filtro original. Preservado para não alterar o DSP nesta etapa.

### Saída de áudio

`src/hal/audio_hal.*`: `audio::begin(44100)`, `audio::write(pcm, frames)`,
`audio::set_volume(0..100)`, `audio::end()`. PCM de entrada é estéreo
intercalado 16-bit. `(int32_t(L)+int32_t(R))/2` é duplicado nos dois slots;
o engine continua estéreo. Chamada futura: `audio::write(out_buf, DMA_BUF_LEN)`.

I2S Philips, master TX, 44.1 kHz, slots 16-bit, MCLK 256×Fs. Mantidos os
256 frames do engine: **5,805 ms por bloco**. Quatro descritores DMA somam
1024 frames / **23,22 ms de capacidade de fila**, mais tempo de render do
mixer e atraso do codec. Esse é limite de buffering estimado, não medição
de latência fim a fim. Escolhidos quatro em vez de oito para reduzir fila;
dropouts/custo de todos os FX ainda precisam ser medidos no milestone 3.

No caminho de PCM não há alocação, I2C, desenho ou Serial. Buffer mono é
estático e a escrita bloqueia apenas por backpressure do DMA, timeout 100 ms
por chamada, evitando busy-loop na task de prioridade máxima no core 0.
Há um único writer; init/end/volume pertencem ao controle fora do realtime.
Em erro de escrita, pode ter ocorrido envio parcial: parar e reinicializar,
sem repetir o bloco inteiro.

`src/hal/guition_board.*` é dono único do barramento moderno I2C_NUM_0.
O touch futuro deverá receber `guition::i2c_bus()`; adaptar a inicialização
GT911 do BSP, que hoje cria outro bus. Não chamar `Wire.begin()` nesses pinos.
O driver ES8311 Espressif está em `lib/es8311`, com
[origem/licença e alterações](lib/es8311/PROVENANCE.md).

Teste host do downmix (GCC C++ disponível):

```powershell
g++ -std=c++17 -Wall -Wextra -Werror tests/pcm_mono_test.cpp -o .pio/pcm_mono_test.exe
.\.pio\pcm_mono_test.exe
```

Verifica overflow nos extremos, conteúdo isolado em L/R, cancelamento,
arredondamento e limites do buffer. Não substitui teste I2S na placa.

## Limite da validação

Compilar não confirma som, pinagem da unidade física ou ausência de dropouts.
Nesta unidade, boot, PSRAM 32 MiB, ACK 0x18 e execução completa do teste foram
confirmados por Serial em 2026-10-05; a confirmação auditiva continua pendente.
Milestone 2 só fica **validado em hardware** após boot, 32 MiB, ACK 0x18 e tom
audível pelo SPEAKER, sem falhas. Registrar revisão, fonte, speaker e logs.
O synth/wavetable dry já está ligado ao HAL e validado por PCM/timing. O usuário
autorizou continuar sem o speaker; a validação auditiva permanece pendente.
Próxima etapa: SDMMC/LDO4, sem streaming. A documentação original do Tab5
permanece abaixo como referência.

---

# M5STACK-TAB5-SAMPLER-DRUM-MACHINE-2026 (original)

![tab5_synth (Pequeña)](https://github.com/user-attachments/assets/57b2cbd7-b851-4b18-aadb-c19e939da05c)

M5STACK TAB5 SAMPLER DRUM MACHINE 2026 - ESP32 P4

This is my ported vsampler/drum machine/groovebox to M5STACK TAB5

Added a lot of features.

Samples
- Samples (wav mono 44100 16bit) are loaded from SD card into PSRAM at start. No need to convert into array
- ADSR
  
Synth
- Added synth engine (upgraded from my previous drum synth. Now is 16bit 44100 instead of 8bit 22050
- Each track can be sampler or synth.
- Multi-wavetable voice.

FX
- FX per channel: Reverb, Delay, Chorus, Flanger, Tremolo, etc
- Sets of presets on every FX

USB HOST
- USB MIDI host. Now is mapped to use with AKI APC KEY25

WHAT YOU NEED:
- M5STACK TAB5 HARDWARE
Shopping link:
https://s.click.aliexpress.com/e/_EvUGwd2
- Arduino IDE 2.3.7
- ESP boards from Expressif (3.3.3)
- M5STACK boards (3.2.5)
- M5Unified Library and dependencies (ArduinoIDE library manager) (3.2.5)
- esp32-usb-host-demos Library from touchgadget https://github.com/touchgadget/esp32-usb-host-demos
- Rest of libraries are already included (I think)
- MicroSD card with (128 max) WAV files (mono 16bit 44100hz) If you dont have SD or is not inserted or no compatible WAV files on SD you can still use machine as synth only. NO SOUNDS ON FLASH

VIDEO DEMO SOUND 1
https://www.youtube.com/watch?v=KDW14Gmv_Tc

## SYSTEM OVERVIEW
Hybrid music production system based on the M5STACK TAB5 with the ESP32-P4 microcontroller. It combines an enhanced synthesis engine with a sample player, designed for live performance and generative musical ideas.

### Audio Engine
* **Quality:** 16-bit / 44,100 Hz (Significant upgrade from the previous 8-bit / 22kHz).
* **Trigger Architecture:** "One Shot" system (Single Note ON, no Note OFF event).
* **Duration Control:**
    * **Synthesizer:** Defined by the `LEN` parameter.
    * **Sampler:** Defined by Start (`INI`) and End (`END`) points.
    * *Note:* INI/END values are mapped in a range from 0 to 2048, offering approximate precision.

---

## USER INTERFACE (GUI)

The interface combines permanent physical controls with contextual page navigation.

### Global Controls (Always visible)
* **Pads:** Trigger sounds, select tracks, and Piano mode keyboard.
* **Navigation:** Buttons for track selection and value adjustment (+/-).
* **SHIFT Function:**
    * Modifies the +/- 10 buttons to count by 100s.
    * When combined with `SOLO`, `MUTE`, or `STEP INIT`, it resets parameters to default values.

### Page Structure
1. **SOUND:** Sound generation and shaping.
2. **GLOBAL:** Sequencer settings and utilities.
3. **FX:** Effects processing.

---

## PAGE: SOUND (Sound Generator)

The system features **16 Tracks**: 8 Synthesizer tracks (Left) and 8 Sampler tracks (Right).

### Sampler Features
* **Source:** WAV files (Mono, 16-bit, 44.1kHz) from SD card.
* **Capacity:** Loads the first 128 compatible files or until the 24MB of reserved PSRAM is full.
* **Fallback:** If no SD card is present, the system operates exclusively as a synthesizer.
* **Envelope:** Full ADSR available for samples.

### Synthesizer Features
* **Engine:** Improved version of the previous "Drum Synth."
* **Envelopes:** 4 predefined types (variations of attack, release, etc.).
* **Detune:** Exclusive parameter to detune the oscillator and thicken the sound.

### Editing Parameters
`PAN`, `VOL`, `FILTER`, `ENVELOPE` (Synth), `MOD` (Synth), `DETUNE` (Synth), `LEN` (Synth), `TYPE`, `A`, `D`, `S`, `R`, `REVERSE`.

### Layering Function
* **ADD NEXT SND:** Allows triggering multiple sounds simultaneously.
    * *Example:* If Track 7 has this value set to 2, triggering Track 7 will also play Track 8.

---

## PAGE: GLOBAL (Sequencer & Utilities)

### Playback Settings
* **BPM:** Global tempo.
* **MASTER VOL:** Overall volume.
* **MELODIC:**
    * *ON:* The engine uses the pitch stored in each step of the sequence.
    * *OFF:* The sound uses the fixed pitch defined in the `PITCH` parameter.
* **SAVE:** Saves the pattern and sounds. Pressing it allows you to change the SAVE type (patterns only or sounds only).

### Composition Tools
* **MOVE:** Shifts the selected pattern to the left or right.
* **SCALE:** Restricts random note generation to a specific musical scale.
* **PIANO MODE:** Allows playing the selected sound chromatically using the PADS.
    * *Press again:* Activates **CLEAR** mode to erase the selected pattern.
* **SONG LOAD MODE:** Defines what is loaded automatically:
    * `0`: Loads Pattern + Sounds.
    * `1`: Pattern Only.
    * `2`: Sounds Only.

### Random Generation
* **RANDOM SND:** Generates a randomized sound design.
* **RANDOM PATTERN:** Generates a rhythmic and melodic sequence.
    * Follows the selected `SCALE`.
    * If Scale = 0, it uses the base PITCH with octave variations (+/- 12 semitones).

---

## PAGE: FX (Effects)

* **Processing:** Utilizes the remaining PSRAM (not occupied by samples).
* **Persistence:** *Warning:* FX settings are NOT saved to Flash memory.
* **Presets:** Most effects use fixed presets for ease of use.
* **Delay:** The only effect with numerical configuration (milliseconds).
* **Repeat:** Note repeat with 4 rhythmic positions (0 = disabled).

---

## MEMORY MANAGEMENT & SONG MODE

### Data Structure
* **Memories:** 16 total Memory Banks.
* **Content:** Each Memory stores 16 different Patterns, each with its own set of sounds.
* **Storage:** All data is saved to internal Flash.

### Load Operations (LOAD)
* **LOAD P:** Loads only the 16 patterns.
* **LOAD S:** Loads only the 16 sounds.
* **LOAD:** Loads both patterns and sounds (default).
* *Startup Note:* Upon power-up, the system does not load any Memory automatically. You must load a Memory before loading a pattern.

### SONG Mode
Automatically chains patterns together.
* Operates within the active "Memory."
* Allows selecting a START pattern and an END pattern to create a playback loop.

M6 adds native 4-bit SDMMC, checked PCM16/44.1 kHz WAV loading to PSRAM,
resident sample voices and a TRACK → SAMPLE assignment page. Put WAV files in
`/P4SDM/SAMPLES/`; mono and stereo (downmixed during load) are supported.
Synth fallback still boots without a card. No formatting is performed.
See [M6 architecture, audit and qualification](docs/GUITION_M6.md).
**PHYSICAL SD VALIDATION PENDING** until a microSD card is available.

M6.1 sample pitch/source semantics and optional physical-card runner: [qualification report](docs/GUITION_M61.md).

## M7 — padrões e performance em RAM

O app agora tem 16 padrões independentes, 16 pistas por padrão e comprimento 1–16.
Toque no campo de padrão no cabeçalho para abrir PATTERN: fila quantizada no fim
do loop, modo INSPECT para editar sem enfileirar, COPY com escolha de destino e
CLEAR com segundo toque de confirmação. TRACK oferece MUTE/SOLO; pads continuam
audicionando mesmo quando a pista está mutada ou fora do solo. Os padrões não
são persistidos e não exigem cartão. A qualificação física SD/sample do M6 segue
pendente, sem bloquear M7.

[Modelo, semântica, testes e qualificação M7](docs/GUITION_M7.md) ·
[Auditoria do sequenciador Tab5](docs/GUITION_M7_TAB5_AUDIT.md).

Build normal: `pio run -e guition_app`; stress contínuo com 16 vozes synth/Delay e
transições reais: `pio run -e guition_app_stress`. Após qualificar, restaure
`guition_app`. Validação do log: `python tests/analyze_m7.py <log>`.


## M8 — Groove engine

`guition_app` inclui velocity 1–127, ACCENT (100 ↔ 127), probability 0–100%,
ratchet 1–4x e swing global 50–75%. Na SEQ, toque um step para alternar ON/OFF
e selecioná-lo; toque STEP para editar. O contorno branco identifica o step
selecionado. SWING fica na SEQ. O editor mostra pattern/track/step, ON/OFF,
VEL numérico, ACCENT, PROB e quatro botões RATCHET. Arraste VEL, PROB ou SWING.

Metadados são retidos ao desligar steps, reduzir comprimento ou usar CLEAR.
COPY copia máscaras, comprimento e groove. Pads manuais usam velocity 127.
PLAY reinicia swing e PRNG; STOP cancela ratchets futuros e mantém tails.
`guition_app_stress` começa com 12 segundos de 16 tracks, 4x, 240 BPM e Delay,
depois exercita groove e operações M7/UI durante a captura de 60 segundos,
com display/touch ativos e sem exigir SD. Veja [relatório M8](docs/GUITION_M8.md)
e `tests/analyze_m8.py`. Persistência, locks, song mode e MIDI ficam fora do escopo.
