#pragma once
#include "Common.h"
#include "Synth.h"

// One table describes every sound-shaping parameter. The processor builds its
// parameters from it, and the matcher searches in the same space.
namespace sp
{
enum Idx { Sub, DiveAmt, DiveTime, Swell, SubDrive, Hold, Trans, TransDecay, Body, BodyDecay,
           BodyDrive, Crunch, Shape, Spice, SpicePos, SpiceLen, SpiceTone, Release, Pitch, Count };

struct Def { const char* id; const char* name; float lo, hi, def, skew; const char* unit; };

inline const std::array<Def, Count>& defs()
{
    static const std::array<Def, Count> d = {{
        { "sub",       "Sub",           -60.f,    6.f,    0.f, 1.0f, "dB" },
        { "diveAmt",   "Dive Amount",     0.f,   36.f,   10.f, 1.0f, "st" },
        { "diveTime",  "Dive Time",       1.f,  500.f,   57.f, 0.5f, "ms" },
        { "swell",     "Swell",           0.f,  300.f,    0.f, 0.5f, "ms" },
        { "subDrive",  "Sub Drive",       0.f,   36.f,    7.1f, 1.0f, "dB" },
        { "hold",      "Hold",           10.f, 4000.f,  500.f, 0.5f, "ms" },
        { "trans",     "Transient",     -60.f,    6.f,  -17.8f, 1.0f, "dB" },
        { "transDecay","Transient Decay", 5.f,  400.f,   70.f, 0.5f, "ms" },
        { "body",      "Body",          -60.f,    6.f,   -7.2f, 1.0f, "dB" },
        { "bodyDecay", "Body Decay",      5.f, 2000.f,  120.f, 0.4f, "ms" },
        { "bodyDrive", "Body Drive",      0.f,   36.f,   17.3f, 1.0f, "dB" },
        { "crunch",    "Crunch",          0.f,    1.f,    0.f, 1.0f, "" },
        { "shape",     "Shape",           0.f,    2.f,    0.f, 1.0f, "" },
        { "spice",     "Spice",         -60.f,    6.f,  -20.f, 1.0f, "dB" },
        { "spicePos",  "Spice Position",  0.f,  600.f,   40.f, 0.6f, "ms" },
        { "spiceLen",  "Spice Length",   10.f,  600.f,  130.f, 0.6f, "ms" },
        { "spiceTone", "Spice Tone",    200.f, 8000.f, 1800.f, 0.4f, "Hz" },
        { "release",   "Release",         5.f, 1000.f,   80.f, 0.5f, "ms" },
        { "pitch",     "Pitch",          20.f,  200.f,   41.2f, 0.6f, "Hz" },
    }};
    return d;
}

struct ParamSet
{
    std::array<float, Count> v;
    ParamSet() { for (int i = 0; i < Count; ++i) v[(size_t) i] = defs()[(size_t) i].def; }
};

inline juce::NormalisableRange<float> range (int i)
{
    const auto& d = defs()[(size_t) i];
    return juce::NormalisableRange<float> (d.lo, d.hi, 0.0f, d.skew);
}
inline float toNorm   (int i, float x)  { return range (i).convertTo0to1 (juce::jlimit (defs()[(size_t) i].lo, defs()[(size_t) i].hi, x)); }
inline float fromNorm (int i, float n)  { return range (i).convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)); }
inline float clampV   (int i, float x)  { return juce::jlimit (defs()[(size_t) i].lo, defs()[(size_t) i].hi, x); }
inline float lin (float db)             { return db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db); }

inline sg::Settings toSettings (const ParamSet& p)
{
    auto V = [&] (int i) { return p.v[(size_t) i]; };
    sg::Settings s;
    s.subGain = lin (V (Sub));      s.transGain = lin (V (Trans));
    s.bodyGain = lin (V (Body));    s.spiceGain = lin (V (Spice));
    s.diveSemis = V (DiveAmt);      s.diveMs = V (DiveTime);
    s.swellMs = V (Swell);          s.subDriveDb = V (SubDrive);
    s.holdMs = V (Hold);            s.releaseMs = V (Release);
    s.transDecayMs = V (TransDecay);
    s.bodyDecayMs = V (BodyDecay);  s.bodyDriveDb = V (BodyDrive);  s.crunch = V (Crunch);
    s.spicePosMs = V (SpicePos);    s.spiceLenMs = V (SpiceLen);    s.spiceHz = V (SpiceTone);
    s.shape = juce::jlimit (0, 2, juce::roundToInt (V (Shape)));
    return s;
}

// Offline render of one note through the same engine the plugin uses.
inline void render (const ParamSet& p, double sr, float* out, int n)
{
    std::fill (out, out + n, 0.0f);
    sg::Engine eng;
    eng.prepare (sr);
    const auto s = toSettings (p);
    eng.noteOn (36, p.v[Pitch], 1.0f);
    for (int i = 0; i < n; i += 512)
        eng.render (out + i, juce::jmin (512, n - i), s);
}
} // namespace sp
