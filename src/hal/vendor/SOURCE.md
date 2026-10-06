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

GT911 register protocol follows the Espressif Apache-2.0 driver bundled by the
same pinned BSP (`examples/DisplayTouchTest/lib/esp_lcd_touch_gt911`). The small
HAL uses the existing board-owned I2C bus directly rather than importing its
independent I2C bus creation. Register facts: status 0x814E, points 0x814F,
product ID 0x8140, dimensions 0x8048; 8 bytes per point, status bit 7 ready,
low nibble point count, acknowledge with status=0. No third-party touch source
is copied; the checked polling wrapper is written for this port.
