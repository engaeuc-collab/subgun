# SubGun

A sample-matching bass synth (VST3) built with [JUCE](https://juce.com).

**Drop in a bass sample -> SubGun rebuilds it with its own engine -> pick from 8 variations -> keep evolving.**

- **Match** tab: drop zone + glowing waveform, a circular 3D waterfall scope (spectrum or waveform, drag to rotate),
  and 8 variation cards. Click a card to hear it and load it into the synth; with *Auto-evolve* on, a new set is
  generated around your choice. *Back* returns to the previous set; *variation spread* controls how far variations stray.
- **Engine** tab: four stems (sub, transient, body, spice) with extruded 3D envelope ribbons, mute/solo and knobs, plus master controls.
- **FX** tab: placeholder for the upcoming effects rack.
- No factory presets.

## Get the plugin without installing anything (GitHub Actions)
1. Push this folder to a GitHub repo (drag-and-drop on github.com works).
2. Open the **Actions** tab -> *Build VST3* runs automatically (about 10 min the first time).
3. Open the finished run -> **Artifacts** -> download `SubGun-Windows-VST3` or `SubGun-macOS-VST3`.
4. Copy `SubGun.vst3` to:
   - Windows: `C:\Program Files\Common Files\VST3\`
   - macOS: `/Library/Audio/Plug-Ins/VST3/` (then `xattr -cr /Library/Audio/Plug-Ins/VST3/SubGun.vst3`; the build is unsigned)

> If the `.github` folder did not upload, use **Add file -> Create new file**, name it `.github/workflows/build.yml` and paste the file contents.

## Build locally
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target SubGun_VST3
```
Output: `build/SubGun_artefacts/Release/VST3/SubGun.vst3`. A `SubGun_Standalone` target is also configured for quick testing.

## Layout
```
Source/Synth.h            DSP engine (voices, four stems)
Source/Params.h           one table of sound parameters shared by plugin + matcher
Source/Matcher.h          matcher interface (implementation lives at the end of PluginProcessor.cpp)
Source/PluginProcessor.*  parameters, MIDI, master chain
Source/Theme.h            glossy look-and-feel, glass panels
Source/Ui.h               3D scope, stem lanes, waveform/drop zone, variation cards
Source/PluginEditor.*     pages and layout
docs/ANALYSIS.md          how the matching works
```

## Roadmap
FX rack (distortion, filter, OTT, chorus) - draggable envelope handles - oversampling on clippers - save/recall variation history.

## Licence note
JUCE is dual-licensed (AGPLv3 / commercial). A plugin you distribute that links JUCE must be released under AGPLv3 or use a JUCE commercial licence. Fine for a public GitHub project; check before selling.
