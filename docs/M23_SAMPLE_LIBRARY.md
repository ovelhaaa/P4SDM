# M23 musical sample library

Host status: **PASS**. Physical installation/loader qualification is in progress;
final evidence and acceptance are recorded below when available. Human musical
acceptance is **PENDING**. No generated sound is represented as a recording.

## Inventory, provenance and rebuild

**42 WAVs**: 11 acoustic recordings by Joseph Sardin / LaSonotheque (individual
CC0 evidence URLs), 15 analog-processed electronic sources from uzu-drumkit
(The Unlicense), 14 original procedural syntheses and 2 ingredient-based loops.
They include 4 kicks, 4 snares, 2 claps, 2 rims, 4 closed hats, 2 open hats,
3 toms, 3 shaker/tambourine sounds, 4 percussion, 4 bass notes, 4 melodic
plucks/mallets/chord, 2 textures, 2 loops and 2 synthetic vowel hits.
The modeled kick is explicitly synthetic. One acoustic hi-hat captures an
open/close articulation; audition it before choosing a closed-hat role.

Sources were actively researched and each download licensed before inclusion.
Per-file URLs, original hashes, author and license evidence are pinned in
`tools/m23_sources.json` and included in `assets/m23/manifest.json`.
[LaSonotheque snare example](https://lasonotheque.org/caisse-claire-6-s2309.html)
documents a real studio recording and individual CC0 redistribution permission.
[uzu-drumkit](https://github.com/tidalcycles/uzu-drumkit) describes electronic and
analog processed origins; these files are never labeled acoustic recordings.
The pinned uzu commit and its license URL are in every relevant manifest entry.

`assets/m23_sources.zip` contains the 26 licensed original source files, pinned
source manifest and full uzu Unlicense for offline rebuilds. Each source is
checked against its original SHA256 before decoding; mismatches abort. Mutable
source discovery (`tools/m23_lock_sources.py`) is separate from the pack builder.
Original M23 synthesis is dedicated to CC0-1.0; loops retain ingredient licenses.
No account/access restriction was bypassed. Attribution is in the pack.

Rebuild: Python with numpy2.2.6/scipy1.15.3, then
`python tools/m23_sample_pack.py`. Output collisions with differing WAV content
abort. Pack output defaults to `assets/m23`; source cache stays in ignored `.pio`.
`tests/m23_sample_pack_test.py` rebuilds into a temporary directory and checks
all files byte-for-byte. `tests/m23_wav_test.cpp` feeds every WAV through the
actual production RIFF parser. No Project persistence format was changed.

## Conversion and numerical quality

Every file is RIFF/WAVE uncompressed signed integer PCM16, **44100Hz mono**.
Input PCM/float is decoded to double, stereo is downmixed with cancellation
ratio checks, and source rates are resampled with rational polyphase filtering.
Leading/trailing quiet tails are trimmed while preserving decay; multi-hit
djembé/tambourine recordings use an explicit first-hit crop. DC removal uses a
tapered correction, 32-frame start fade and up to441-frame end fade. Category
ceilings attenuate over-level sounds; quieter hits are not normalized to full
scale. Seeded bass/formant/modal synthesis supplies tonal roles rather than a
noise-only diagnostic palette. Loops mix existing hits with circular tails,
exact one-bar lengths at100/120BPM and zero boundary discontinuity.

WAV storage: **3,718,480 bytes** (3.55MiB). Largest resident PCM: **260,244 bytes**
(0.248MiB); every sample is far below4MiB. Total pack PCM is below3.55MiB,
compatible with simultaneous kit loading and the established4MiB reserve.
All42 have nonzero RMS, zero digitally clipped frames, zero endpoint step and
absolute mean below one PCM count. Peak/RMS/DC, source downmix ratio, duration,
frame count, root/BPM, gain/fade and SHA256 are recorded per file. Mathematical
checks establish format/quality properties, not pleasantness or listening
acceptance. Physical waveform output does not substitute for human listening.

## Kits and on-device use

`assets/m23/kits.json` gives16 absolute Track assignments for hybrid, electronic
and organic/acoustic-led kits. The organic kit includes modeled kick, bass and
mallets; it is not an all-recorded kit. For the hybrid kit:

|Track|Role|
|---|---|
|01|Modeled kick|
|02|Electronic kick|
|03|Recorded acoustic snare|
|04|Electronic snare|
|05|Clap|
|06|Recorded closed hat|
|07|Electronic open hat|
|08|Shaker|
|09|Recorded low tom|
|10|Recorded high tom|
|11|Recorded djembé|
|12|Rim|
|13|Recorded agogo|
|14|C2 bass pluck|
|15|C4 mallet|
|16|Synthetic AH vowel|

In the normal browser, select a Track and load its listed WAV. Sample pitch60
plays the original source/root; transpose relative to that. Loops require the
listed tempo and manual timing (no beat stretching). No saved Projects are
overwritten or created automatically. MIDI channel10 notes36..51 correspond to
these absolute Track numbers in the opt-in candidate.

## Physical SD and qualification

Windows exposes COM13 Serial/JTAG, not a mounted card drive. Actual baseline
firmware mounted the8GB SDMMC card with9 existing WAVs. A dedicated temporary
`guition_m23_sd_transfer` firmware owns the filesystem exclusively. It never
formats or touches Projects; writes only validated M23 names to the flat
`/P4SDM/SAMPLES`, with documentation under `/P4SDM/M23`. Browser limit is checked.
256-byte chunks use Adler32, explicit offsets and temporary `.m23part` files.
Interrupted partials resume at bounded offsets or are safely removed when
invalid; SHA256 checks the closed/reopened partial and renamed final file.
Existing differing WAVs abort; differing documentation gets a hash-suffixed name.
Normal firmware is restored after transfer, including failure paths. Diagnostic
images are archived to avoid losing restoration inputs during other builds.

The first transfer attempt exposed the small default Serial/JTAG RX buffer;
the diagnostic enlarged it to2048 and the host paces64-byte USB writes. Rejected
attempt logs remain separate. A later interrupted file is resumed without
overwriting verified files. Final hash evidence: `docs/m23/sd_hashes.json`.

Loader qualification uses an ignored copy of the inherited serial console with
production DSP/placement and unmodified storage loader, waveform, transient and
audio paths. It dynamically resolves actual browser indexes; loads every sample
and compares frame counts, then loads the16-Track hybrid kit, exercises pitch,
Reverse, slicing/Gate/transient analysis and simultaneous playback. Its edits
are in RAM only. Human touch/audible feedback remains separate. Detailed final
results: `docs/m23/loader_results.json` and `LOADER_SERIAL.log`; normal boot is
captured after restoration. No physical PASS is claimed before these finish.
