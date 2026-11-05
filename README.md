# SubGun

A VST3 bass-gun / tearout-sub synth built with [JUCE](https://juce.com). Four stems
(**sub, transient, body, spice**) with pitch-dive, hard-clipped sub, noisy attack layers,
and a live **3D waterfall visualiser** (spectrum or waveform, drag to rotate).
FX are stubbed in the UI and will be added next.

## Get the plugin without installing anything (GitHub Actions)

1. Push this folder to a GitHub repo (drag-and-drop on github.com works).
2. Open the **Actions** tab -> *Build VST3* runs automatically (about 10 min the first time).
3. Open the finished run -> **Artifacts** -> download `SubGun-Windows-VST3` or `SubGun-macOS-VST3`.
4. Copy `SubGun.vst3` to:
   - Windows: `C:\Program Files\Common Files\VST3\`
   - macOS: `/Library/Audio/Plug-Ins/VST3/` (then run `xattr -cr /Library/Audio/Plug-Ins/VST3/SubGun.vst3`, because the build is unsigned)

> If the `.github` folder did not upload via drag-and-drop, use **Add file -> Create new file** on GitHub,
> type `.github/workflows/build.yml` as the name, and paste the contents of that file.

## Build locally

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target SubGun_VST3
```
Output: `build/SubGun_artefacts/Release/VST3/SubGun.vst3`. A Standalone app target (`SubGun_Standalone`) is also set up for quick testing.

## Layout

```
Source/Synth.h            DSP engine (voices, stems)
Source/PluginProcessor.*  parameters, MIDI, master chain, preset loading
Source/Ui.h               3D scope, extruded envelope lanes, knobs
Source/PluginEditor.*     window layout
Source/Presets.h          presets derived from the reference samples
docs/ANALYSIS.md          what the sample analysis found
```

## Roadmap
- FX rack (distortion, filter, OTT-style multiband, chorus/width) in the red `fx` strip
- Draggable envelope handles directly on the 3D lanes
- Oversampling on the clipper stages; sample-match analyser; preset save/load

## Licence note
JUCE is dual-licensed (AGPLv3 / commercial). A plugin you distribute that links JUCE must either
be released under AGPLv3 or use a JUCE commercial licence. Fine for a public GitHub project; check before selling it.
VST is a trademark of Steinberg; VST3 is free to use under Steinberg's licence terms.
