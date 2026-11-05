# Reference sample analysis

Six reference one-shots (`gun_burp`, `gun_cracked`, `gun_FUCK`, `gun_fucklebitch`, `gun_newbie`,
`gun_resampleone`) were analysed (STFT + autocorrelation) to decide what the engine needs.

| Finding | Value | Where it went in SubGun |
|---|---|---|
| Sub fundamental dives from ~54-70 Hz to a steady ~38-43 Hz | about 7-12 semitones over 50-170 ms | `Dive Amount` / `Dive Time` (squared curve) |
| ~30-35% of samples sit at the peak in 5 of 6 files | sub is hard-clipped into a flat-topped wave | `Sub Drive` (tanh into near-square) |
| Harmonics of the sub are tiny (-25 to -35 dB) | the clipped wave is band-limited; width comes from other layers | sub is kept separate from body |
| First 250 ms is bright: spectral centroid 5-7 kHz, 35-50% of energy above 2 kHz | noisy transient that decays to pure sub by ~0.3-0.5 s | `Transient` stem (HP noise + falling click) |
| Some files (cracked, burp) keep a second bright burst after the transient | mid-attack "bark" | `Spice` stem (windowed, two-hump FM/noise burst) |
| `newbie` is a different animal: 2.3 s, unclipped, harmonics fall like 1/n (1, .5, .33, .25...) | a long sustained saw-like bass | `Body` stem with long decay + `Newbie Saw` preset |
| Presets `Gun Burp`, `Gun Cracked`, `Gun Heavy`, `Gun Low`, `Newbie Saw`, `Resample` | hand-tuned from the above | `Source/Presets.h` |

Caveat: the presets are starting points tuned from measured numbers, not an automatic sample match.
Adding a "match this sample" analyser is a good later feature.
