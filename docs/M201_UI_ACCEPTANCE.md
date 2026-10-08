# M20.1 — corrective validation and physical acceptance status

Baseline: merged M20, `46fc01508b4a525a62673b41246eeafe911edd7f`.

## Exact CI correction

The prior [GitHub Actions run](https://github.com/ovelhaaa/P4SDM/actions/runs/37811781868)
failed at `python tests/render_ui.py`. Ubuntu GCC 13 diagnosed possible
truncation of `P%02d / T%02d / LEN %d / SEED %08X` into the 48-byte intermediate
buffer in `draw_content()`.

The only firmware change is `char s[48]{}` → `char s[80]{}`. This remains a
fixed-size stack buffer passed to bounded `snprintf`. Final display truncation
through `ui20::bounded_text()` and widget clipping are unchanged. `-Werror` and
format diagnostics remain enabled. There is no navigation, DSP, detector,
command ownership, persistent-model, or stress-workload change.

Both `python tests/render_ui.py` and `python tests/render_ui.py --baseline`
passed locally. The complete local host/analyzer suite passed **81/81**,
including the inherited checks, exact Engine/event/RNG/Repeat equivalence,
layout/text/navigation/hit checks, and zero host render allocations. Project
remains **V2 / 52,704 bytes**, with identical encoded bytes.
All **25/25** firmware configurations compiled from the corrected source,
including every inherited diagnostic/stress variant, normal application, and
the two unchanged M20 stress configurations. `git diff --check` is clean.

## Remote workflow result

Corrected firmware commit: `bafd442b385b92727982eb235299cfd7628446eb`.
The complete [GitHub Actions run 63](https://github.com/ovelhaaa/P4SDM/actions/runs/37821039630)
finished **SUCCESS** on Ubuntu 24.04. Its job `113461821168` completed every
inherited check and all four previously unreachable steps:

* `python tests/render_ui.py`: **SUCCESS**, GCC 13 with `-Werror`.
* Both `guition_ui20_stress` and `guition_ui20_synth_stress` builds: **SUCCESS**.
* M20 SAMPLE device-log analyzer: **SUCCESS**.
* M20 SYNTH device-log analyzer: **SUCCESS**.

This is a confirmed remote result, not a local-pass substitute. The final
[PR #12 checks](https://github.com/ovelhaaa/P4SDM/pull/12/checks) additionally
track the head including this evidence note and fresh captures; they must
complete successfully before the corrective pass is reported ready.

## Fresh device stress evidence

The SAMPLE run used the unchanged M20/M19 dense workload on the connected board,
13,782 audio blocks over 80 seconds. `analyze_m20.py` passed with every inherited
assertion enabled. Timing: p50 2,915 us, p95 3,390 us, p99 3,515 us, maximum
4,384 us. Against the **5,804.989 us deadline**, worst-case headroom is **24.5%**.
Deadline misses, I2S failures, timeouts, and rails were all zero. PSRAM and
internal heap stayed 24,265,252 / 217,852 bytes; largest allocation was unchanged.
Slider/playhead dirty transfers stayed 49,152 / 24,576 bytes.

The physical SD remained mounted at 4-bit / 20 MHz with nine indexed files.
This verifies card initialization/indexing, not the unobserved finger-operated
load/save workflow. The original M20 captures are preserved separately.

The SYNTH run also completed 13,782 blocks over 80 seconds and passed the same
inherited analyzer chain. Timing: p50 3,751 us, p95 4,121 us, p99 4,234 us,
maximum 4,497 us: **22.5%** worst-case headroom. Deadline misses, I2S failures,
timeouts, and rails were zero. PSRAM/internal heap stayed 26,427,944 / 242,780
bytes, with unchanged largest allocation. Slider/playhead dirty transfers stayed
49,152 / 24,576 bytes. No workload or timing assertion was relaxed.

Evidence: [fresh SAMPLE capture](GUITION_M201_SAMPLE_STRESS_SERIAL.log),
[fresh SYNTH capture](GUITION_M201_SYNTH_STRESS_SERIAL.log).
The corrected normal `guition_app` firmware was then restored. Its
[short startup capture](GUITION_M201_NORMAL_SERIAL.log) verifies initialization;
it is not a physical touch/listening qualification.

## Physical touchscreen acceptance

No mechanism available in this session can physically press the JC4880P443
touchscreen or observe its LCD. The connected USB serial interface can upload
firmware and measure audio stress; it cannot provide real finger observations.
Human operation and observations were requested. No physical acceptance is
inferred from the host renders or serial output.

| Required inspection/workflow | Actual physical result |
|---|---|
| Six sections: SEQ, TRACK, SAMPLE, PATTERN, PERFORM, PROJECT | Not performed/observed |
| A: Track selection, steps, velocity/probability/ratchet, return to SEQ | Pending human operation |
| B: real SD WAV load, Playback, Slice, Auto Analyze/Apply/Cancel | Pending human operation |
| C: Step and Basic/Tone/Sample locks | Pending human operation |
| D: Fill/Override, Mix, momentary Repeat x2/x4/x8, Chain | Pending human operation |
| E: physical SD Save As/Save, leave, Load | Pending human operation |
| Header context/dirty/transport/BPM/performance indicators | Not physically observed |
| Waveform, active/inspected slices, canonical/proposed markers | Not physically observed |
| Synth-disabled controls and sample-source refresh | Not physically observed |
| Track/Pattern 16, BPM 400, slice 16/16, choke 8, long names/errors, Chain x16, locks | Not physically exercised |
| Rapid sliders/context changes/playhead/Repeat/tabs and stale pixels | Not physically observed |

No additional UX polish was made: there is no concrete physical observation
on which to base such a change. Remaining physical UX annoyances are unknown.
Existing M20 truncation and compact pixel typography remain documented in
[the M20 report](M20_UI_REPORT.md).

**After real finger operation on the JC4880P443, are there any remaining visible
overlaps, overwritten labels, major stale-pixel issues, confusing navigation
dead ends, or unreliable primary controls?**

**Not determined.** Real finger operation was not observed, so neither absence
nor presence of these issues can be certified. Software validation can pass
while physical M20.1 acceptance remains pending.

No M21, interpolation, or other DSP work is included.
