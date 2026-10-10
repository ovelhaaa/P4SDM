# M23 USB-MIDI input

Status: **PARTIAL**. Parser, descriptors, queue, ownership and opt-in firmware
are implemented and host tested. No physical MIDI controller has been connected;
controller routing, reconnect, USB traffic impact and VBUS safety are unqualified.
Production remains MIDI-disabled. M24 is not started.

## Baseline and invariants

Source baseline: `a1235d417ccf9d68a12834d13955914e43f334e3` (M22 / PR17).
Initial untracked `docs/m21/error.png` was preserved. Baseline firmware images
were copied to ignored `.pio/m23-baseline` before edits. The production flags
remain interpolation=1, Linear32=1, magnitude=1, cache=0, placement=1. Framework
remains pioarduino 55.03.36-1, Arduino 3.3.6 / IDF5.5, ESP32-P4 **ES** v1.3.
Project V2 remains **52,704 bytes**. No DSP, PCM allocation or scheduling
mathematics changed. MIDI-disabled code compiles out all MIDI work.

`docs/m23/BASELINE_BOOT_SERIAL.log` is fresh COM13 evidence: USB-Serial/JTAG,
chip v1.3, ES8311/I2S 44.1k/16 and SD mount/index initialization. Display/touch
drivers are inherited; no human visual, touch or audible observation is claimed.
Baseline host checks initially lacked numpy/Pillow (113/115); after installing
dependencies both missing checks passed. Final host suite: **118/118 PASS**.
Build and capture evidence is under `docs/m23`; historical M22 captures remain
historical. Release images are locally archived in `.pio/m23-release`.

## Board USB and power audit

Inspected actual vendor schematic sheets 02 and 03 from
[ultramcu board repository](https://github.com/ultramcu/guition-jc4880p443c-i-w/tree/master/schematic).
Sheet03: **USB2** is Full Speed (`USB1P1_N/P`); **USB3** is High Speed
(`ESP_USB_N/P`). COM13 reports USB-Serial/JTAG; this is the FS programming/log
connection. The HS OTG controller is the candidate host. Firmware selects P4
HS controller bit0 (`peripheral_map=1`); pinned IDF's
`USB_DWC_LL_GET_HW(0)` maps HS and `(1)` FS. No FS host takeover is attempted.

Both USB-C connectors have 5.1k CC pull-downs and their VBUS pins connect to
**USB5V_IN**. These are device/sink role terminations, not evidence of a compliant
Type-C host/source port. Sheet02 connects that rail to the input protection /
IP5306 supply path. The inspected sheets do not provide a dedicated Host VBUS
load switch, enable GPIO, current limit, Type-C role controller or safe source
current rating. No GPIO/3.3V power source was added. Do not attach a bus-powered
controller expecting qualified host power. Qualification needs verified connector
adaptation, power/current/backfeed measurements and an appropriate self-powered
controller or externally powered hub. Pinned SDK has hub support, but that does
not qualify a specific hub or its power behavior.

Reference APIs: [ESP-IDF 5.5 P4 Host Library](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-reference/peripherals/usb_host.html).
Local pinned `usb_host.h`, SDK config and P4 USB low-level mapping were checked.
The original Tab5 `USB_tools.ino` / `usbhhelp.hpp` path assumes external helpers,
bulk endpoints and unchecked four-byte receive loops. It is not reused; its
`FEATURE_USB_MIDI=0` flag stays disabled. New candidate uses `P4SDM_USB_MIDI=1`.

## Input architecture

`src/midi` separates descriptors, USB host, packet decoding, musical events,
mapping/ownership and the SPSC queue. One library daemon and one asynchronous
client task run below touch/audio priority on core1. Only the client task
produces MIDI events; only audio consumes them. The existing UI/audio queue
remains single-producer. USB callbacks never access Engine/Voice/Project state.
The 128-slot queue holds 127 events, rejects overflow, counts loss and advances
device generation after loss. Audio processes at most 32 events per block;
disconnect/generation/loss/project handoff clears only MIDI-owned gates.
Old-generation events are rejected. No audio wait or audio allocation is added.

Enumeration reads VID/PID/configuration and walks bounded descriptor lengths.
Only Audio/MIDIStreaming protocol0 with **bcdMSC=1.00** and a discovered bulk-IN
endpoint is accepted. Interface/alternate/endpoint/max packet are discovered;
an OUT endpoint is unnecessary. One controller/interface and cable0 are routed.
MIDI2/UMP without a compatible legacy interface is explicitly rejected.
Receive allocation is one endpoint max packet (8..64 FS, 512 HS), submission
length is a packet multiple. Completion resubmits; failures/disconnect advance
generation and initiate halt/flush. Teardown waits for completion before freeing,
then releases interface/device. Other devices are counted/rejected. Root Host
remains installed for reconnect. Runtime endpoint errors tear down that session;
unplug/replug is required to retry it. Install failure returns without preventing
normal instrument startup. Multi-controller routing is out of scope.

Parser validates CIN/status/data lengths, cable/channel and NoteOn velocity0.
NoteOn/Off, CC, Program and realtime Start/Stop/Continue/Clock are recognized.
Unsupported system common, pitch bend, pressure and SysEx are counted without
access beyond a packet. Incomplete transfer tails are discarded/count malformed;
never concatenated with another transfer/device. Padding bytes beyond CIN length
are ignored. Channel representation inside events is zero-based (9 = channel10).

## Musical behavior and ownership

Channel10, cable0, notes36..51 map absolute Tracks01..16, independent of UI bank.
Base note is compile-time `P4SDM_MIDI_BASE_NOTE` outside Project V2. Accepted
NoteOn velocity1..127 goes through `resolve_event` / existing velocity gain;
Track volume is unchanged. Existing source, pan, filter/send, choke, slice,
mute/solo and playback settings are used. No melodic keyboard mode is enabled.

Each normal trigger, source change, sample replacement and project application
invalidates MIDI ownership. A MIDI claim attaches device generation, cable,
channel, note and audio trigger serial to the resident Track voice. NoteOff can
release only that matching owner. Same-note retriggers retain an outstanding-on
count: an earlier matching NoteOff cannot release the newest retrigger; balanced
offs release after the last outstanding on. MIDI1 cannot distinguish a spurious
duplicate off from a legitimate identical note's off; missing offs require CC123,
Stop or disconnect. This is monophonic per Track, not polyphony. SAMPLE release
uses existing semantics (OneShot ignores ordinary release; Gate fades). SYNTH
NoteOff stops the owned synth amplitude using the existing amplitude controls.
Ownership tests cover wrong note/device, retrigger, sequencer invalidation and
channel-scoped clear.

Start uses existing Play/start semantics (restart accepted transport position).
Stop uses existing stop semantics and releases sequenced gates plus MIDI-owned
gates. Neither reconnect nor clock changes sequencer position. **Continue is
parsed/diagnosed but does not resume transport** because inherited Play resets
position. CC120/123 clear only matching MIDI owners; CC7/10/74 and Program are
parsed but unused, avoiding silent persistent live edits. No MIDI learn/output.

## Timing and acceptance limits

Each event carries receive, parse, queue timestamps and generation. Audio records
accepted count and maximum receive-to-parse, parse-to-enqueue and queue-to-block
delay; client prints these diagnostics outside audio. At 44100/256, block period
is **5804.989 us**. These are software boundaries, not pad-to-speaker latency;
receive timestamp starts when IDF dispatches the callback. No measured MIDI
latency/jitter values are claimed without a controller. Queue overflow under
load is counted; bounded drain can span multiple blocks.

Clock diagnostics count 24PPQN pulses, aggregate intervals/BPM and min/max jitter,
detect >250ms gaps and stale input. Start/Stop state is diagnostic. Generation
change resets estimator. **No external clock production sync/BPM update** exists;
phase/timing qualification is pending M23.1.

Host tests exercise all CIN/status combinations, malformed/tails, legacy vs UMP
descriptors, IN-only nondefault endpoint/interface, cable/channel filtering,
velocity, ownership, overflow/generation rejection, concurrent SPSC ordering,
transport and estimator. CI retains all inherited checks and adds production,
Nearest, MIDI candidate, SAMPLE and SYNTH stress builds and sample-pack tests.
Physical controller descriptors, actual routing/velocity/Gate/reconnect, input
onset and sustained **USB traffic** headroom remain **PENDING**. Idle Host stress
cannot establish USB workload acceptance. MIDI remains **PARTIAL** until these
physical tests pass with >=20% worst-case audio reserve and zero audio faults.
