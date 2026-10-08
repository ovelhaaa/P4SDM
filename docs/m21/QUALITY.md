# M21 objective comparison

Actual C++ lookup; PCM16 sources; 2048 output frames starting at logical frame 256; Q16 ratios from unchanged pitch table. Reference: 384 taps, symmetric Blackman window, normalized DC gain, cutoff min(1,1/ratio). Edge replication. No voice fades in this reconstruction comparison. Full results: quality.csv. Reference sensitivity to doubling the kernel to 768 taps is recorded in reference_convergence.csv.

Spurious energy is residual mean square after least-squares fundamental/DC fit. CSV separates harmonic energy (orders 2–5 below Nyquist) from remaining nonharmonic energy. Above-Nyquist cases report folded-tone RMS separately; their sinc reference removes source content above output Nyquist. None of the production methods is an antialias resampler.

| Source Hz | Pitch | Nearest RMS | Linear RMS | Hermite RMS |
|---:|---:|---:|---:|---:|
| 100 | 48 | 55.22 | 0.21 | 0.21 |
| 100 | 53 | 91.95 | 0.37 | 0.29 |
| 100 | 59 | 91.96 | 0.36 | 0.29 |
| 100 | 60 | 0.00 | 0.00 | 0.00 |
| 100 | 61 | 92.21 | 0.37 | 0.30 |
| 100 | 67 | 88.16 | 0.37 | 0.32 |
| 100 | 72 | 0.20 | 0.20 | 0.20 |
| 500 | 48 | 285.98 | 4.90 | 0.23 |
| 500 | 53 | 457.93 | 5.25 | 0.30 |
| 500 | 59 | 465.91 | 5.25 | 0.31 |
| 500 | 60 | 0.00 | 0.00 | 0.00 |
| 500 | 61 | 464.48 | 5.24 | 0.30 |
| 500 | 67 | 469.74 | 5.23 | 0.32 |
| 500 | 72 | 0.20 | 0.20 | 0.20 |
| 1000 | 48 | 570.59 | 20.09 | 0.21 |
| 1000 | 53 | 918.78 | 20.95 | 0.48 |
| 1000 | 59 | 929.67 | 20.90 | 0.48 |
| 1000 | 60 | 0.00 | 0.00 | 0.00 |
| 1000 | 61 | 930.50 | 20.90 | 0.49 |
| 1000 | 67 | 936.27 | 20.95 | 0.49 |
| 1000 | 72 | 0.20 | 0.20 | 0.20 |
| 3000 | 48 | 1708.09 | 181.67 | 6.13 |
| 3000 | 53 | 2743.95 | 187.96 | 11.69 |
| 3000 | 59 | 2780.81 | 187.97 | 11.67 |
| 3000 | 60 | 0.00 | 0.00 | 0.00 |
| 3000 | 61 | 2775.50 | 188.09 | 11.67 |
| 3000 | 67 | 2783.87 | 187.88 | 11.70 |
| 3000 | 72 | 0.19 | 0.19 | 0.19 |
| 5000 | 48 | 2835.52 | 501.85 | 46.25 |
| 5000 | 53 | 4534.18 | 518.47 | 63.71 |
| 5000 | 59 | 4549.11 | 518.39 | 63.65 |
| 5000 | 60 | 0.00 | 0.00 | 0.00 |
| 5000 | 61 | 4588.31 | 518.70 | 63.68 |
| 5000 | 67 | 4601.39 | 518.43 | 63.76 |
| 5000 | 72 | 0.19 | 0.19 | 0.19 |
| 10000 | 48 | 5580.33 | 1945.65 | 652.24 |
| 10000 | 53 | 8742.24 | 2006.37 | 708.72 |
| 10000 | 59 | 8777.00 | 2005.53 | 708.59 |
| 10000 | 60 | 0.00 | 0.00 | 0.00 |
| 10000 | 61 | 8834.67 | 2006.51 | 708.85 |
| 10000 | 67 | 8860.33 | 2005.12 | 708.72 |
| 10000 | 72 | 0.33 | 0.33 | 0.33 |
| 15000 | 48 | 8146.80 | 4150.96 | 2671.02 |
| 15000 | 53 | 12308.92 | 4265.42 | 2736.69 |
| 15000 | 59 | 12404.96 | 4265.95 | 2736.99 |
| 15000 | 60 | 0.00 | 0.00 | 0.00 |
| 15000 | 61 | 12421.76 | 4268.18 | 2738.26 |
| 15000 | 67 | 11289.98 | 7906.29 | 9557.53 |
| 15000 | 72 | 11312.27 | 11312.27 | 11312.27 |
| 18000 | 48 | 9570.01 | 5724.85 | 4678.71 |
| 18000 | 53 | 14048.45 | 5872.65 | 4726.19 |
| 18000 | 59 | 14189.09 | 5872.87 | 4726.62 |
| 18000 | 60 | 0.00 | 0.00 | 0.00 |
| 18000 | 61 | 14196.82 | 5877.08 | 4729.31 |
| 18000 | 67 | 11311.39 | 7031.97 | 8567.66 |
| 18000 | 72 | 11311.16 | 11311.16 | 11311.16 |

Cubic overshoot (65,536 phases each):

| Fixture | Pre min | Pre max | Saturated phases | Clipping RMS |
|---|---:|---:|---:|---:|
| positive_plateau | 32767.00 | 40958.88 | 65535 | 5982.50 |
| negative_plateau | -40959.88 | -32768.00 | 65535 | 5982.50 |
| alternating | -32768.00 | 32767.00 | 0 | 0.00 |
| step | -32768.00 | 32766.50 | 0 | 0.00 |

Reference 384→768 taps: worst sine RMS change 12.47 PCM units; all per-case changes are in reference_convergence.csv.

Generated matched-control audition WAVs are in .pio/m21-quality (ignored). No actual listening or copyrighted musical recordings are claimed.
