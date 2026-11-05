# How the sample matching works

Dropping a sample runs this on a background thread (`Source/Matcher.cpp`):

1. **Load** - any WAV/AIFF/FLAC/OGG/MP3, mixed to mono, leading silence trimmed, capped at 2.5 s, peak-normalised.
2. **Analyse** (seeds the search near the answer)
   - steady fundamental from an 8192-point FFT -> `pitch`
   - pitch track of the first ~100 ms -> `dive amount`, `dive time`
   - fraction of samples at the peak -> `sub drive` (flat-topped / clipped subs)
   - active length of the envelope -> `hold`
   - high-frequency vs low-frequency energy early on -> `transient` level and decay
3. **Fit** - a (1+6) evolution strategy mutates the 19 sound parameters in normalised space (about 460 renders).
   Each candidate is rendered through the same engine the plugin uses and compared to the sample with a
   multi-resolution log-band spectrum (2048 and 512 point) plus a level-envelope term. Frames are weighted by loudness.
4. **Vary** - the best fit becomes card 1 (*Recreation*). Seven more cards apply directed changes
   (Heavier, Cleaner, Long Dive, Punchy, Bright, Gritty, Mutant) plus random jitter. The *variation spread* slider scales both.
5. **Evolve** - choosing a card makes it the parent and repeats step 4 around it, so you can walk toward a sound.
   *Back* restores the previous set.

Each card shows the variation's waveform (cyan/violet) over the original (white ghost) and a match score
(`100 * exp(-loss / 10)`). The score is a rough guide to similarity, not a perceptual metric.

## What the original six reference samples looked like
Sub fundamental dives from about 54-70 Hz to about 38-43 Hz over 50-170 ms, is hard-clipped (about 30% of samples at the peak),
and sits under a bright noisy attack for the first ~250 ms. One sample (`newbie`) is instead a long, unclipped saw-like bass.
The engine's four stems (sub, transient, body, spice) were designed around these traits.

## Limits
- It models bass one-shots of this kind. A very different sound (vocal, pad) will be approximated by the closest stem mix.
- Noise is deterministic, so a hit sounds the same every time.
- The search is stochastic and time-limited (about 5-15 s); dropping the same sample twice gives slightly different fits.
