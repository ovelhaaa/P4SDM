# Guition display hardware reference

`board_p4_st7701_init.h` is copied verbatim from ultramcu/guition-jc4880p4-bsp,
commit `324970bade0d1f4e52880fe8016580368bc1e06e`, `src/board_p4_st7701_init.h`.
MIT license and copyright are retained; see LICENSE-guition.txt.

https://github.com/ultramcu/guition-jc4880p4-bsp/tree/324970bade0d1f4e52880fe8016580368bc1e06e

Display HAL adapts the same DSI LDO3/2500mV, 2 lanes/500Mbps, DBI commands,
DPI 34MHz and porch timings, reset delays, RGB565 double buffering and PPA
270-degree counter-clockwise rotation. It does not copy SD, Wi-Fi or LVGL.
It adds checked teardown, an explicit PSRAM logical buffer and conservative
two-refresh framebuffer reuse fencing in the UI task.

M4.1 audits the actual bundled IDF commit `f56bea3d1f`, rather than the
upstream v5.5 tag. Its `components/esp_lcd/dsi/esp_lcd_panel_dpi.c` uses
separate DMA completion and bridge VSYNC callbacks. Refresh events have no
framebuffer identity, so the two-refresh fence is retained. For native dirty
drawing, modified tiles are explicitly written back before the native-FB
`draw_bitmap` branch selects the buffer; a valid one-row call triggers only
an extra row's cache writeback, not a full-frame copy. This branch is coupled
to the pinned SDK and must be re-audited on SDK upgrades. Hardware panel
initialization, timings, resets and touch protocol remain as above.

GT911 register protocol follows the Espressif Apache-2.0 driver bundled by the
same pinned BSP (`examples/DisplayTouchTest/lib/esp_lcd_touch_gt911`). The small
HAL uses the existing board-owned I2C bus directly rather than importing its
independent I2C bus creation. Register facts: status 0x814E, points 0x814F,
product ID 0x8140, dimensions 0x8048; 8 bytes per point, status bit 7 ready,
low nibble point count, acknowledge with status=0. No third-party touch source
is copied; the checked polling wrapper is written for this port.
