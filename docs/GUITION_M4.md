# M4 — display, touch and audio coexistence

Implementation and first hardware run on the Guition JC4880P443C_I_W,
ESP32-P4 ES revision 1.3 at 360 MHz. This milestone is **not yet qualified**:
the six-mode audio run fails in three windows. The user physically confirmed
correct orientation, colors, borders, corner/center touches and dragging.

## Hardware and rendering

The ST7701S command table is copied verbatim, including its MIT license,
from ultramcu/guition-jc4880p4-bsp commit
324970bade0d1f4e52880fe8016580368bc1e06e. Provenance and pin mappings are in
`src/hal/vendor/SOURCE.md`. Native DSI is 480×800, RGB565, two 500 Mbps lanes,
34 MHz pixel clock, reset GPIO5, backlight GPIO23 and LDO3 at 2500 mV.
The logical drawing surface is landscape 800×480. PPA rotates 270 degrees
counterclockwise; native `(479-y, x)` and logical `(raw_y, 479-raw_x)` are
inverses. The host test checks every logical pixel and the four corners.
This proves coordinate arithmetic, not the physical panel mounting.

There are two native scanout buffers and one logical buffer, all in PSRAM:
3 × 768000 = 2304000 payload bytes. Display initialization consumes
2305228 PSRAM bytes and 1704 internal heap bytes in the real capture.
The driver flips to a native framebuffer without CPU copying. The UI waits
for two refresh callbacks before reusing the previous scanout buffer; this
conservative fence accounts for the driver's interrupt sampling order.
PPA and cache synchronization still touch an entire frame on every update.
Light mode redraws only selected logical rectangles, but does not use a
partial framebuffer transfer. Heavy mode clears and redraws the whole frame.

GT911 uses the existing I2C0 handle (SDA7/SCL8), reset GPIO3, first probing
0x5d and then 0x14. The board responds at 0x5d with product ID 911 and a
480×800 coordinate range. ES8311 remains at 0x18. The HAL adds/removes only
its GT911 device, never creates or deletes the shared bus. Status and point
packets are read and acknowledged with bounded transactions. The most recent
pressed state persists until a fresh release packet arrives. No Wire bus,
SD initialization or sampler loading is involved.

## Standalone physical test

`guition_display` draws RGB bars, white borders, axes, center target,
TL1/TR2/BR3/BL4 markers, a counter and touch feedback. Real initialization,
panel presentation, codec/touch probes, bus identity and audio shutdown all
return ESP_OK. The raw capture `GUITION_M4_DISPLAY_SERIAL.log` contains
thousands of updates with zero reported touch errors. Its initial segments
contain no press/release events; the appended post-restoration segment
records26 presses and26 releases with zero errors. For example native
`(467,481)` maps to logical`(481,12)`, and native`(123,293)` to
logical`(293,356)`, matching the declared rotation. In response to the physical test
prompt, the user confirmed "Tudo correto, círculo acompanha o dedo": correct
horizontal corner markers, colors/border and finger tracking through the
requested corners, center and drag. This is human verification, not a claim
of exhaustive numerical corner coverage in the serial log. The standalone pattern
is restored after the combined measurement for continued physical testing.

## Combined workload and measurements

`guition_ui_audio` runs the unchanged original synth render and Delay with
16 continuously active voices, MIDI48–63, wavetables0–15, pan -120 in steps
of16, envelope3/length127, modulation64, voice volume80, detune0, master60
and filter0. Delay has 88200 samples, time12000, feedback120, input160,
return100 and voice mask0xffff. No other FX buffers are allocated. No DSP
coefficient, buffer length, signal quality or voice count was reduced.

Audio uses core0, priority24, 256 stereo frames at 44100 Hz. Its render
budget is 5804.988662 us. UI workers use priority2 and are placed on core1
for static/light/heavy windows, then core0 for the same windows. Touch uses
core1 priority3 at a requested100 Hz, publishing a lock-free snapshot to UI.
UI requests30 FPS for light/heavy. UI drawing and waits remain outside the
audio task. Workers use static stacks; audio timing uses fixed arrays and
reports only after the measurement. There are no UI locks, serial reports,
allocations or I2C calls inside the timed audio render loop.

Each mode measures517 blocks (3.001 seconds of generated audio); the six
windows run consecutively with live transport and Delay history. Notes and
wave positions reset between windows, but Delay state does not. A UI update
already in progress can cross a mode boundary. The core0 static window's
two misses are its first two blocks, following core1 heavy rendering; they
are retained in the report rather than discarded as warmup. Wall windows
differ slightly because the transport is already live at measurement start.

Raw evidence is `GUITION_M4_UI_AUDIO_SERIAL.log`. `tests/analyze_m4.py`
checks all3102 ordered block timings, recomputes distributions and confirms
16 active voices. SAFE means no render misses and at least20% reserve at
the worst observed render; TIGHT means no misses but less reserve; any
render miss is FAIL. Comparison uses the earlier M3.2 Delay p99=2898 us
and max=3592 us. It is a historical reference, not a paired identical run.

The hardware report is **FAIL**, not an audio coexistence pass. Core1
parallel updates produce large render contention. Core0 light/heavy remain
SAFE for their individual windows, at the cost of approximately20/8.6 FPS.
The requested30 FPS is not achieved. Prefer low-priority UI on core0 for
the next iteration, then requalify transitions and longer interactive runs;
core placement alone is not proven sufficient by this run.

All windows have zero transport errors/timeouts, zero silent blocks, zero
rail frames and zero UI/touch API errors. Cycle crossings include blocking
I2S write and scheduling jitter; they are not DMA underrun counts. Playback
starvation has no reliable public counter here. Audible output remains
unverified. Touch polling achieves about100 Hz but no human touch
events were captured in this combined run.

Both heaps and largest PSRAM block remain unchanged across all measured
windows with the same live transport/tasks. Display remains allocated after
audio shutdown; comparing boot and shutdown would not be a leak test.
The tables below are generated directly from the genuine serial capture.

## Validation

All six environments build locally. GitHub Actions run37400942633 passes
all six builds, PCM host checks, FX safety, original/current FX equivalence
and the display geometry host test. Firmware source commit5322c7c was used
for the hardware measurements; only the host serial reset sequence changed
afterward. Combined firmware SHA256:
`17e0e91960fa8ae67d8138a1d3d89fa6d874bf4e789441c991027560b2e854a9`.
Standalone firmware SHA256:
`79afdc1dcbead44574868a98660a7179f1f27d47e5052b46b4835fcd3fc06ee7`.
Standalone build uses23892 static RAM bytes and403784 flash bytes;
combined build uses89516 static RAM bytes and523808 flash bytes. These
linker sizes exclude runtime PSRAM framebuffer/Delay allocations.

Reproduce with PlatformIO6.1.18 and the platform pinned in `platformio.ini`:
build/upload `guition_ui_audio`, then capture COM13 with
`python tests/capture_guition.py --output docs/GUITION_M4_UI_AUDIO_SERIAL.log --seconds 55`
and analyze with
`python tests/analyze_m4.py docs/GUITION_M4_UI_AUDIO_SERIAL.log`.
After capture closes, build/upload `guition_display` to restore the physical
test pattern. `--append` preserves genuine successive captures;
`--no-reset` attaches without interrupting a running standalone test.
The default Windows USB/JTAG capture asserts DTR/RTS on open, drops DTR,
then pulses RTS to reset. Opening with both initially low did not reset this
board and produced an empty capture after the one-shot test had completed;
that empty capture is not used as validation evidence.

## Measured tables

| Stage | UI core | p99 us | Max us | Delta p99/max vs M3.2 Delay us | Headroom p99/worst us | Render misses | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A_STATIC | 1 | 2925 | 3647 | 27/55 | 2879.989/2157.989 | 0 | SAFE |
| B_LIGHT | 1 | 5746 | 7085 | 2848/3493 | 58.989/-1280.011 | 3 | FAIL |
| C_HEAVY | 1 | 6977 | 7071 | 4079/3479 | -1172.011/-1266.011 | 181 | FAIL |
| A_STATIC | 0 | 2939 | 6617 | 41/3025 | 2865.989/-812.011 | 2 | FAIL |
| B_LIGHT | 0 | 3767 | 3776 | 869/184 | 2037.989/2028.989 | 0 | SAFE |
| C_HEAVY | 0 | 3767 | 3783 | 869/191 | 2037.989/2021.989 | 0 | SAFE |

| Stage/UI core | Min us | Average us | p50 us | p95 us | p99 us | Max us | First block us | Top 3 block:us |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A_STATIC/1 | 2860 | 2916.46 | 2915 | 2922 | 2925 | 3647 | 3647 | 0:3647, 205:2928, 294:2926 |
| B_LIGHT/1 | 2907 | 3260.78 | 2925 | 5396 | 5746 | 7085 | 5412 | 3:7085, 1:6672, 2:6609 |
| C_HEAVY/1 | 2868 | 4378.38 | 3025 | 6855 | 6977 | 7071 | 3870 | 413:7071, 208:7033, 299:6996 |
| A_STATIC/0 | 2901 | 2935.27 | 2916 | 2925 | 2939 | 6617 | 6387 | 1:6617, 0:6387, 2:5327 |
| B_LIGHT/0 | 2868 | 3121.97 | 2929 | 3724 | 3767 | 3776 | 2868 | 214:3776, 171:3773, 231:3771 |
| C_HEAVY/0 | 2864 | 3390.45 | 3658 | 3733 | 3767 | 3783 | 2874 | 369:3783, 50:3780, 150:3771 |

| Stage/UI core | Blocks/active | Peak L/R/mono | Nonzero frames | Silent blocks | Rail frames | Write errors/timeouts | Cycle crossings |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A_STATIC/1 | 517/16 | 4725/6074/5133 | 132278 | 0 | 0 | 0/0 | 175 |
| B_LIGHT/1 | 517/16 | 4757/6035/5103 | 132288 | 0 | 0 | 0/0 | 162 |
| C_HEAVY/1 | 517/16 | 4757/6035/5103 | 132287 | 0 | 0 | 0/0 | 257 |
| A_STATIC/0 | 517/16 | 4757/6035/5103 | 132287 | 0 | 0 | 0/0 | 192 |
| B_LIGHT/0 | 517/16 | 4757/6035/5103 | 132287 | 0 | 0 | 0/0 | 196 |
| C_HEAVY/0 | 517/16 | 4757/6035/5103 | 132287 | 0 | 0 | 0/0 | 131 |

| Stage/UI core | Requested FPS | Achieved FPS | Frames | Update avg/max us | Skipped frames | UI errors | Touch polls/Hz | Touch errors | Fresh touch/press/release |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A_STATIC/1 | 0 | 0.000 | 0 | 0.00/0 | 0 | 0 | 299/100.210 | 0 | 0/0/0 |
| B_LIGHT/1 | 30 | 20.325 | 61 | 49922.95/68709 | 30 | 0 | 300/99.961 | 0 | 0/0/0 |
| C_HEAVY/1 | 30 | 14.987 | 45 | 66883.18/99174 | 45 | 0 | 300/99.911 | 0 | 0/0/0 |
| A_STATIC/0 | 0 | 0.000 | 0 | 0.00/0 | 0 | 0 | 300/100.021 | 0 | 0/0/0 |
| B_LIGHT/0 | 30 | 19.993 | 60 | 50336.43/93241 | 30 | 0 | 300/99.963 | 0 | 0/0/0 |
| C_HEAVY/0 | 30 | 8.644 | 26 | 116401.85/148843 | 64 | 0 | 301/100.072 | 0 | 0/0/0 |

| Stage/UI core | PSRAM before/after | Largest block before/after | Internal before/after |
| --- | --- | --- | --- |
| A_STATIC/1 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |
| B_LIGHT/1 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |
| C_HEAVY/1 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |
| A_STATIC/0 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |
| B_LIGHT/0 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |
| C_HEAVY/0 | 30884856/30884856 | 30408692/30408692 | 485864/485864 |

| Snapshot | PSRAM free bytes | PSRAM largest bytes | Internal free bytes |
| --- | --- | --- | --- |
| before display | 33551856 | 33030132 | 501944 |
| after display | 31246628 | 30932980 | 500240 |
| after touch | 31245360 | 30932980 | 500036 |
| after Delay FX initialization | 30884904 | 30408692 | 500036 |
| combined running before measurement | 30884856 | 30408692 | 485864 |
| after UI audio stress before shutdown | 30884856 | 30408692 | 485864 |
| after audio shutdown display remains active | 30884904 | 30408692 | 491156 |

[M4] heap_loss psram=0 internal=0 workers_done=7 audio_shutdown=ESP_OK drain_errors=0
[M4] touch_coverage_bits=0 event_overflow=0 codec_probe=ESP_OK touch_probe=ESP_OK
[M4] result=FAIL audio_PCM_render_and_transport_only audible_unverified DMA_starvation_metric_unavailable
Validated six modes, 3102 ordered audio block timings and the genuine 16-voice workload.
