# Auditoria do fork e escopo desta entrega

Repositório local confirmado: origin `https://github.com/ovelhaaa/P4SDM.git`.
Upstream é referência; nenhuma alteração é enviada ao upstream.
Branch de trabalho: `codex/guition-audio-bringup`.

## Acoplamentos encontrados (linhas do código original)

| Subsistema | Local | Dependência / ação futura |
|---|---|---|
| Boot Tab5 | `DRUM_2026_VSAMPLER_TAB5_2002.ino:796–807` | `M5.config`, `M5.begin`, external_spk, `M5.Speaker.config`, volume 255; substituir inicialização pelo board/audio HAL |
| Saída PCM | `synthESP32.ino:369–375` | `out_buf` estéreo e loop de retry em `M5.Speaker.playRaw`; trocar somente envio por `audio::write(out_buf, DMA_BUF_LEN)` depois do tone test físico |
| Jack/speaker | sketch principal:17–30, 767–785, 929–945 | `ADDR_EXP=0x43`, registros 0x03/0x05/0x0F, `PIN_SPK=0x02`, `PIN_JACK=0x80`, `comprobar_jack`, `M5.In_I2C`, `M5.update`; excluir da inicialização Guition |
| Display | sketch:65, 820–829, 869–896; `LCD_tools.ino` | `M5.Display`, `M5Canvas waveSprite`, cores LGFX; HAL gráfico pequeno e sprite local |
| Touch | sketch:80; `touch.ino:4–10` | `lgfx::touch_point_t`, `getTouchRaw`, `convertRawXY`; substituir aquisição por GT911 mantendo hit testing |
| microSD | sketch:41–55, 669–764 | `SPI.begin`, `SD.begin(...40 MHz)`, `SD.open`; HAL SDMMC 4-bit e LDO4 antes de mount |
| USB | sketch:85–94, 562–570, 853–867; `USB_tools.ino` | USB Host ESP-IDF + `show_desc.hpp` / `usbhhelp.hpp` externos e não presentes no fork; isolar por feature antes de reativar |
| Persistência | `files_tools.ino:16,34,64,84`; sketch:844–847 | SPIFFS, não SD; conservar formato de memória, revisar partição e evitar format-on-failure no firmware final |
| FX/memória | `fx.h:93–110,178–188,306–315,470–479` | Reverb usa `std::vector<float>` sem alocador explícito SPIRAM; delay/chorus/flanger usam PSRAM com fallback interno; verificar falhas antes de executar DSP |

## Inventário gráfico

Métodos ativos `M5.Display`: `color565`, `drawRect`, `fillRect`, `fillScreen`,
`fontHeight`, `print`, `printf`, `println`, `setBrightness`, `setCursor`,
`setRotation`, `setTextColor`, `setTextSize`, `textWidth`, `getTouchRaw`,
`convertRawXY`. `setFont` aparece somente em comentário.

Sprite: `createSprite`, `fillSprite`, `drawLine`, `setCursor`, `setTextColor`,
`print`, `pushSprite`. Não basta implementar retângulos e texto.

`fillBPOS()` começa em `LCD_tools.ino:1014`: pads terminam em x=1280/y=720;
há outras áreas fixas, como fillRect(160,0,960,300). A UI precisa de novo layout
800×480; rotacionar o painel sozinho não resolve. As tabelas de hit testing
compartilham os mesmos objetos de coordenadas, o que permite preservar lógica.

## Arquivos preserváveis

- `wavetables.h`, `tablesESP32_E.h`, `synthESP32LowPassFilter_E.h`: dados e DSP.
- `fx.h`: algoritmos preserváveis; revisão posterior de alocação/falhas.
- `seq.h`, `sequencer.ino`: estrutura musical preservável; revisar API de timer
  e contexto do callback (usa FromISR, mas esp_timer por padrão despacha em task).
- `button.h`, `rot.h`: modelos da UI, sem dependência M5; `rot.h` possui guarda
  `#ifndef ROT_H` / `#define ROT` inconsistente, risco ao migrar para vários .cpp.
- `keys.ino`, `rots.ino`: lógica de controles preservável; dependem de globais e
  funções da UI, não são unidades C++ independentes.
- `files_tools.ino`: formato de patterns/sounds preservável; separar backend FS.
- `USB_tools.ino`: preservar para etapa USB posterior.
- `synthESP32.ino`: preservar mixer, sampler, envelopes, filtros, PAN e FX;
  abstrair somente saída e tratamento de erro/task.

Todos os arquivos originais permanecem intactos nesta entrega de milestone 2.
O `.ino.bin` existente é firmware Tab5 e **não serve para a Guition**.

## Riscos para os próximos milestones

1. P4 ES rev 1.3 requer `chip_variant=esp32p4_es` e plataforma fixada; registrar
   revisão real no boot. Board definition do BSP e pioarduino 55.03.36-1 usados.
2. O projeto Arduino concatena .ino e gera protótipos; compilar cada .ino como
   .cpp sem organizar declarações/globais não funciona. Diagnóstico usa `src/`
   próprio e não compila os .ino da raiz.
3. Headers USB externos, M5Unified/M5GFX e tipos LGFX bloqueiam build monolítico;
   diagnóstico não depende de nenhum deles. FEATURE_USB_MIDI=0 aplica-se apenas
   ao novo firmware, não constitui port USB dos arquivos antigos.
4. WAV loader valida offsets fixos, não verifica retorno de reads nem todos os
   limites/chunks RIFF/padding; falha de malloc fica silenciosa. Corrigir somente
   ao migrar storage; manter mono/16/44100/128 e teto ~24 MiB sem streaming.
5. Limite de samples não considera framebuffer/FX. Reservar margem, maior bloco
   e checar todos os buffers; adicionar relatórios após display, FX e samples
   quando esses subsistemas forem habilitados. Diagnóstico reporta boot/audio.
6. Boot original inicia task de áudio antes de terminar estado das vozes/ADSR;
   mover criação da task para depois da inicialização no milestone 3.
7. Handles USB/LCD reutilizados no original; corrigir na integração multitarefa.
8. BSP GT911 cria `I2C_NUM_0` privadamente. Adaptar para receber
   `guition::i2c_bus()`; não linkar o BSP sem essa integração de ownership.
9. Tone test não estabelece ausência de dropouts no mixer completo. Medir tempo
   de render, underruns e pior caso com todos os FX na etapa de engine.

## Menor conjunto para boot + tone test

1. Board JSON com licença/origem, PlatformIO e duas variantes boot/audio.
2. Board HAL dono do I2C 7/8 e relatório PSRAM.
3. ES8311 Espressif vendorizado com transporte I2C moderno, I2S TX moderno,
   downmix mono no HAL e PA11 controlado fora do engine.
4. Diagnóstico Serial, 440 Hz/5 s, volume atenuado e falha explícita.
5. Compilar ambos; flash e confirmação auditiva na placa são a próxima
   validação obrigatória. Só então ligar o mixer original no milestone 3.

## Referências verificadas

- [BSP Guition, commit 324970b](https://github.com/ultramcu/guition-jc4880p4-bsp/tree/324970bade0d1f4e52880fe8016580368bc1e06e): board, pinos, display/PPA, SD/LDO e ownership I2C.
- [Hardware e esquemáticos](https://github.com/ultramcu/guition-jc4880p443c-i-w): folhas 01 (codec) e 02 (amplificador/conector).
- [Driver ES8311 Espressif](https://github.com/espressif/esp-bsp/tree/c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a/components/es8311): sequência de configuração, propagação de erros, coeficientes 44.1 kHz.
- [ES8311 Arduino Audio Driver](https://github.com/pschatzmann/arduino-audio-driver/blob/8a65c604ee1cc2471e4ae049a9e67355bb931a31/src/Codecs/es8311/ES8311.h): comparação decode-only; não usado como dependência.

Arduino Audio Tools/I2SCodecStream foi avaliado como alternativa. O I2S direto
evita dependência de stream/resampling e deixa número de descritores, timeout e
backpressure explícitos; isto é uma escolha arquitetural, não benchmark.
