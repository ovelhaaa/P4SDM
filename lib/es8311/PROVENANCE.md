# ES8311 provenance and local changes

Source: https://github.com/espressif/esp-bsp/tree/c9a0f385cd1bf86e1bd07f14dc2cb77b86f20a9a/components/es8311

Copied `es8311.c`, `include/es8311.h`, `priv_include/es8311_reg.h`, and
Apache-2.0 `LICENSE`. The register sequence and frequency coefficient table
are Espressif's, including 11,289,600 Hz MCLK / 44,100 Hz playback.

Local changes:

- Replace legacy `driver/i2c.h` with `driver/i2c_master.h`.
- Include FreeRTOS/task explicitly (the legacy I2C header used to supply them).
- `es8311_create()` attaches a 400 kHz device to a caller-owned bus, checks
  allocation/attachment failure; delete detaches it but never destroys the bus.
- Register reads/writes use modern transmit/transmit_receive, with 100 ms
  control-plane timeout and original `esp_err_t` propagation.
- Set SDP_OUT register 0x0A bit 6 (capture serial port disabled), as done for
  decode-only mode in Espressif/Arduino Audio Driver ES8311 implementations.
  Analog initialization remains Espressif's; this does not claim ADC power-off.

This modified component is for playback only. Future capture work must restore
the serial output enable and configure the input path explicitly.
