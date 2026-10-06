# M5 original Tab5 UI audit (before Guition implementation)

Sources: `DRUM_2026_VSAMPLER_TAB5_2002.ino`, `LCD_tools.ino`, `touch.ino`, `sequencer.ino`, `seq.h`, `synthESP32.ino`.

The original uses sound/global/FX pages (`rPage` 0/1/2), sixteen sound pads and a sixteen-step row. `selected_sound` selects the parameter owner; pad triggering and step editing share UI refresh flags. Patterns are sixteen bitmasks, with optional per-step melodic pitches. The clock is 24 PPQN and emits a sixteenth every six ticks. It supports first/last steps, song-mode pattern/sound-set changes, selected pattern, mute bits and solo bits: muted tracks never trigger; any solo mask restricts playback to those soloed tracks. No swing implementation was found in these clock sources.

`playing`, `tick`, `sstep`, `pattern`, `melodic`, `mutes`, `solos`, `bpm`, `ROTvalue`, selection and refresh flags are shared globals. UI setters can mutate engine parameters and transport directly. The Guition shell replaces that ownership arrangement only in its separate build target.

Original BPM UI bounds are 0..400; timer enforces at least 1. M5 chooses 30..400. Volume 0..127, pan -127..127, MIDI pitch 0..127, length 0..127, modulation 0..127 and wavetable selection retain their original meanings. Length remains the original synth envelope-length index, not a new ADSR decay parameter. FX has per-track send masks and global returns. The original includes sample selection, WAV browsing, SD/preset loading and song-mode storage assumptions; these are excluded from M5.

M5 retains sixteen tracks and one fixed sixteen-step pattern, exposes volume/pan/pitch/length/wave and per-track mute, and defers solo, variable pattern length, melodic editing, song mode and storage. Its temporary sounds are distinct wavetable/pitch voices; drum labels would overstate their acoustic realism.
