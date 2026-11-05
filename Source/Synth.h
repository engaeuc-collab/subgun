#pragma once
#include "Common.h"

// SubGun engine: four "stems" layered per voice
//   sub        pitch-diving sine, hard-driven into a flat-topped (clipped) wave
//   transient  highpassed noise burst + falling sine click
//   body       detuned band-limited saws an octave up, driven + crunched
//   spice      windowed FM / band-passed noise burst (two humps) at a chosen position
namespace sg
{
struct Settings
{
    // linear gains (0 when muted / off)
    float subGain = 1, transGain = 0.1f, bodyGain = 0.4f, spiceGain = 0.1f;
    float diveSemis = 10, diveMs = 57, swellMs = 0, subDriveDb = 7;
    float holdMs = 500, releaseMs = 80;
    float transDecayMs = 70;
    float bodyDecayMs = 120, bodyDriveDb = 17, crunch = 0;
    float spicePosMs = 40, spiceLenMs = 130, spiceHz = 1800;
    int   shape = 0; // 0 exp, 1 lin, 2 log
};

// r = remaining level 0..1 (1 at start, 0 at end)
inline float shapeCurve (float r, int shape)
{
    r = juce::jlimit (0.0f, 1.0f, r);
    switch (shape)
    {
        case 0:  return r * r * r;                         // exp: fast then slow
        case 1:  return r;                                 // lin
        default: { const float p = 1.0f - r; return 1.0f - p * p * p; } // log: holds, then drops
    }
}

inline float polyBlep (float t, float dt)
{
    if (t < dt)         { t /= dt;                 return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt)  { t = (t - 1.0f) / dt;     return t * t + t + t + 1.0f; }
    return 0.0f;
}

class Voice
{
public:
    bool active = false, releasing = false;
    int note = 0;
    uint32_t order = 0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sr, 512u, 1u };
        tHP.prepare (spec);  bodyHP.prepare (spec);  spiceBP.prepare (spec);
        tHP.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        tHP.setCutoffFrequency (4500.0f);
        bodyHP.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        bodyHP.setCutoffFrequency (90.0f);
        spiceBP.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        spiceBP.setCutoffFrequency (1800.0f);
        spiceBP.setResonance (2.5f);
    }

    void start (int midiNote, float hz, float velocity, uint32_t startOrder)
    {
        note = midiNote;  order = startOrder;
        vel = 0.6f + 0.4f * velocity;
        baseHz = hz;
        rng.setSeed (1337);   // deterministic noise: the matcher and the plugin render identically
        active = true;  releasing = false;
        t = 0.0;  relSamples = 0.0;
        subPh = bodyPh1 = bodyPh2 = clickPh = spicePh = 0.0;
        crushVal = 0.0f;  crushCount = 0;
        tHP.reset();  bodyHP.reset();  spiceBP.reset();
    }

    void release() { releasing = true; }
    float timeMs() const { return (float) (t * 1000.0 / sr); }

    void render (float* out, int n, const Settings& s)
    {
        const float srf = (float) sr;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float subDrive  = juce::Decibels::decibelsToGain (s.subDriveDb);
        const float subNorm   = 1.0f / std::tanh (subDrive);
        const float bodyDrive = juce::Decibels::decibelsToGain (s.bodyDriveDb);
        const float relMs     = juce::jmax (5.0f, s.releaseMs);
        const float relSamp   = relMs * 0.001f * srf;

        for (int i = 0; i < n; ++i)
        {
            const float tms = (float) (t * 1000.0 / sr);

            // master gate: hold -> fade, plus note-off release
            float gate = 1.0f;
            if (tms > s.holdMs) gate = 1.0f - (tms - s.holdMs) / relMs;
            if (releasing)      { gate *= 1.0f - (float) relSamples / relSamp; relSamples += 1.0; }
            if (gate <= 0.0f)   { active = false; return; }

            // ---- pitch dive (shared by sub + body)
            const float r = juce::jlimit (0.0f, 1.0f, 1.0f - tms / juce::jmax (1.0f, s.diveMs));
            const float f = baseHz * std::exp2 (s.diveSemis * (r * r) / 12.0f);

            // ---- SUB
            subPh += f / sr;  subPh -= std::floor (subPh);
            float sub = std::sin (twoPi * (float) subPh);
            sub = std::tanh (subDrive * sub) * subNorm;
            const float att = s.swellMs > 0.5f ? juce::jmin (1.0f, tms / s.swellMs) : 1.0f;

            // ---- BODY
            const float fb = f * 2.0f;
            const float dt1 = juce::jmin (0.45f, fb / srf);
            const float dt2 = juce::jmin (0.45f, fb * 1.006f / srf);
            bodyPh1 += fb / sr;           bodyPh1 -= std::floor (bodyPh1);
            bodyPh2 += fb * 1.006 / sr;   bodyPh2 -= std::floor (bodyPh2);
            const float w1 = 2.0f * (float) bodyPh1 - 1.0f - polyBlep ((float) bodyPh1, dt1);
            const float w2 = 2.0f * (float) bodyPh2 - 1.0f - polyBlep ((float) bodyPh2, dt2);
            float body = std::tanh (bodyDrive * 0.5f * (w1 + w2));
            if (s.crunch > 0.001f)
            {
                const int hold = 1 + (int) (s.crunch * 24.0f);
                if (++crushCount >= hold) { crushCount = 0; crushVal = body; }
                const float fold = std::sin (crushVal * (1.0f + s.crunch * 5.0f) * 1.5707963f);
                body = juce::jmap (s.crunch, body, 0.5f * (crushVal + fold));
            }
            body = bodyHP.processSample (0, body);
            const float bodyEnv = shapeCurve (1.0f - tms / juce::jmax (1.0f, s.bodyDecayMs), s.shape);

            // ---- TRANSIENT
            float trans = 0.0f;
            const float te = shapeCurve (1.0f - tms / juce::jmax (1.0f, s.transDecayMs), 0);
            if (te > 0.0f)
            {
                const float nz = tHP.processSample (0, rng.nextFloat() * 2.0f - 1.0f);
                clickPh += (70.0f + 1100.0f * std::exp (-tms / 5.0f)) / sr;
                const float click = std::sin (twoPi * (float) clickPh) * std::exp (-tms / 10.0f);
                trans = std::tanh (1.5f * (0.8f * nz + 0.9f * click)) * te;
            }

            // ---- SPICE
            float spice = 0.0f;
            if (tms >= s.spicePosMs && tms < s.spicePosMs + s.spiceLenMs)
            {
                const float p = (tms - s.spicePosMs) / juce::jmax (1.0f, s.spiceLenMs);
                float w = std::sin (twoPi * p);  w *= w;                     // two humps
                if ((i & 15) == 0)
                    spiceBP.setCutoffFrequency (juce::jlimit (100.0f, 0.45f * srf, s.spiceHz * (1.6f - 1.1f * p)));
                const float b = spiceBP.processSample (0, rng.nextFloat() * 2.0f - 1.0f) * 3.0f;
                spicePh += (double) (s.spiceHz * 0.25f * (2.0f - p)) / sr;  spicePh -= std::floor (spicePh);
                const float fm = std::sin (twoPi * (float) spicePh + 2.0f * b);
                spice = std::tanh (2.5f * (0.7f * b + 0.6f * fm)) * w;
            }

            const float y = sub * s.subGain * att
                          + trans * s.transGain
                          + body * s.bodyGain * bodyEnv
                          + spice * s.spiceGain;
            out[i] += y * gate * vel;
            t += 1.0;
        }
    }

private:
    double sr = 44100.0, t = 0.0, relSamples = 0.0;
    double subPh = 0, bodyPh1 = 0, bodyPh2 = 0, clickPh = 0, spicePh = 0;
    float baseHz = 55.0f, vel = 1.0f, crushVal = 0.0f;
    int crushCount = 0;
    juce::Random rng { 1337 };
    juce::dsp::StateVariableTPTFilter<float> tHP, bodyHP, spiceBP;
};

class Engine
{
public:
    void prepare (double sr) { for (auto& v : voices) { v.prepare (sr); v.active = false; } }

    void noteOn (int note, float hz, float vel)
    {
        Voice* pick = nullptr;
        for (auto& v : voices) if (! v.active) { pick = &v; break; }
        if (pick == nullptr)
        {
            pick = &voices[0];
            for (auto& v : voices) if (v.order < pick->order) pick = &v;
        }
        pick->start (note, hz, vel, ++counter);
    }

    void noteOff (int note)
    {
        for (auto& v : voices) if (v.active && ! v.releasing && v.note == note) v.release();
    }

    void allOff() { for (auto& v : voices) v.active = false; }

    void render (float* out, int n, const Settings& s)
    {
        for (auto& v : voices) if (v.active) v.render (out, n, s);
    }

    // time (ms) of the newest sounding voice, or -1
    float newestTimeMs() const
    {
        const Voice* best = nullptr;
        for (auto& v : voices) if (v.active && (best == nullptr || v.order > best->order)) best = &v;
        return best != nullptr ? best->timeMs() : -1.0f;
    }

private:
    std::array<Voice, 4> voices;
    uint32_t counter = 0;
};
} // namespace sg
