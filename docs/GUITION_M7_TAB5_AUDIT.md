# M7 — original Tab5 semantics audit (before implementation)

Baseline: main `ef941a738a936ea561fd679f69d1a21295b32a9b` / accepted M6.1.

Inspected `DRUM_2026_VSAMPLER_TAB5_2002.ino` declarations, `files_tools.ino`, `sequencer.ino`, `keys.ino` and `LCD_tools.ino`.

| Original behavior | M7 decision |
|---|---|
| `pattern[16]` is the active set of 16 track bitmasks; `memory_pattern[16][16]` holds 16 saved slots. `load_pattern` memcpy replaces the active array. | Preserve 16 slots × 16 tracks × 16 bits. Each slot lives in audio-owned RAM; no shared-global memcpy from UI. |
| `selected_pattern` is an indicator updated by manual load/save or song advance; manual `keys.ino` loads immediately. | Separate edit selection, playing slot and pending slot. Manual queue switches at loop end; INSPECT edits without queueing. |
| `melodic[16][16]` / `memory_melodic` store per-step MIDI pitch; `isMelodic` picks stored pitch versus track pitch. | Defer per-step melodic notes. Preserve M6.1 track/source pitch, including independent synth/sample pitch. |
| Global `firstStep`, `lastStep`, `newLastStep`: clock wraps at old/new end, adopts new last step, resumes at first step. | Start fixed at zero; per-slot length 1–16. Length changes take effect at the next rational clock onset without moving a head mid-step. |
| `mutes` / `solos` are global performance masks; sequencer tests mute then any-solo membership. Pads audition directly. | Preserve gating and multi-solo. Mute stays track performance state; solo is a 16-bit performance mask. Pads bypass both. Neither is copied/cleared with patterns. |
| `mySEQ` advances with timer/task notifications at 24 PPQN and triggers on every sixth tick. | Retain qualified M5 exact rational sample-clock sixteenths, independent of display/timer notifications. |
| Song loop updates `pattern_song_counter`, loads masks and/or sound set according to `song_mode`, and changes selected pattern. | Defer song mode, chains, sound-set switching, first/last song slots. No dependency from M7 engine to song state. |
| Pattern load/save uses SPIFFS files and melodic payload; sound banks store rotary parameters. | No M7 persistence, storage requirements or file access. Existing M6 sample subsystem remains separate. |
| FX retrigger/recording/random/first-step controls use shared globals. | Defer these features; do not copy unsafe ownership. |

Pattern data contains musical masks and length only. Track/source parameters remain separate; UI capture, copy/confirm mode, pending switch, clock phase, telemetry, sample pointers and queues are runtime state. Future serialization must write named musical fields, not raw C++ structure padding or pointers.
