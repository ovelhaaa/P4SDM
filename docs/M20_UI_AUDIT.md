# M20 initial UI audit (current main, before edits)

The working tree was clean on `main`. Audio command ownership, the framebuffer,
touch queues, and Project V2 are the preservation boundary for this milestone.

| Existing page | Entry and return | Problem / proposed section |
|---|---|---|
| Sequence | SEQ footer; Step and Tools shortcuts | Default SEQ; eight pads plus bank cover 16 tracks |
| Track | TRACK footer; Tone, Sample links | TRACK; synth Length/Wave currently look active on sample tracks |
| Fx | FX footer; Delay, Project, Chain, Performance | Miscellaneous hub; retire from navigation, retain Delay in Tone |
| Sample | Track Sample link; Playback link | SAMPLE / Browser; assignment and errors need their own bounded panel |
| SamplePlayback | Sample Playback; Slice; Back | SAMPLE / Playback |
| SampleSlice | Playback Slice; Transients; Back | SAMPLE / Slice; 14 actions crowd waveform; disclose slice operations separately |
| AutoSlice | Slice Transients; Back | SAMPLE / Auto; proposal validity controls Apply |
| Pattern | Header pattern control | PATTERN / Select; playing, queued, editing must remain distinct |
| Step | Sequence STEP | PATTERN / Step; selected step must persist |
| Locks | Step LOCKS 1/3 | PATTERN / Locks / Basic |
| ToneLocks | Locks next category | PATTERN / Locks / Tone; numeric paging obscures categories |
| SampleLocks | ToneLocks next category | PATTERN / Locks / Sample; unsupported context must be explicit |
| Tools | Sequence Tools; Step return | PATTERN / Tools; Track/Generate/Pattern operations and confirmation retained |
| Tone | Track Tone; Back to Track | TRACK / Tone; include global delay toggle |
| Project | FX Project; Back to FX | Direct PROJECT; Home/Browser/Naming/Confirm remain modal states |
| Chain | FX Chain; Back/Performance | PERFORM / Chain; six rows and twelve small editor buttons |
| Performance | FX Performance; Mixer/Repeat/Back | PERFORM / Patterns and Mix; mode-dependent labels obscure navigation |
| PerformanceRepeat | Performance Mixer then Repeat | PERFORM / Repeat; preserve gesture release recovery |

## Geometry and text findings

The `widget(id)` table owns IDs 0–221, while header, waveform, sample status,
and diagnostics also draw at independent coordinates. Rendering and hit-testing
repeat page membership ranges. These duplicate assumptions make additions risky.

* Header title spans x16 onward, while widget 44 begins x184; long context text
  has no clipping. BPM/control widgets share the same unbounded text layer.
* Track's independent title clears x24..240 at y90..150 from inside drawing
  widget 43. It is a hidden cross-widget drawing side effect.
* Diagnostics clear y80..98. Slice notice and Auto Slice status draw at y82;
  diagnostics overwrite those statuses.
* Existing footers start y420; slice/transient actions start y404 and end y460.
  They cannot coexist with a persistent footer at y416.
* Performance legend draws y464 inside the future bottom navigation zone.
* Four Basic lock rows extend through y360, then Clear occupies y362; the next
  category is an unrelated-looking footer. Wave lock's explanatory label exceeds
  the normal toggle width. Pattern copy confirmation also exceeds its button.
* Chain controls/rows are 48 px high, below the requested finger target.
* Repeat has generous targets but instructional copy consumes useful area.
* Sample slice mixes inspected selection, active selection, equal division,
  audition, track changes, and reset without hierarchy.

No engine feature should be deleted. Persistent six-section navigation, common
subtabs, bounded widget drawing, and an executable visible-widget registry will
replace the reachable navigation/geometry paths. Legacy numeric behavior routes
may remain as a compatibility adapter for the existing qualification scripts.

Physical observations have not been made during this audit; these are source
findings, not a touchscreen qualification claim.
