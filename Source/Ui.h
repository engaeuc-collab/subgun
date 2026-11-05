#pragma once
#include "PluginProcessor.h"

namespace ui
{
inline juce::Font F (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
// Rotary knob with a faux-3D look
class KnobLNF : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (7.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);

        // track + value arc
        juce::Path track, val;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        val.addCentredArc   (c.x, c.y, r, r, 0.0f, startAngle, ang, true);
        g.setColour (juce::Colour (0xff23262e));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // knob cap: drop shadow, lit body, rim light
        const float cr = r - 6.0f;
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (c.x - cr + 1.5f, c.y - cr + 3.0f, cr * 2.0f, cr * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4e5a), c.x - cr * 0.5f, c.y - cr * 0.7f,
                                                 juce::Colour (0xff101216), c.x + cr * 0.6f, c.y + cr * 0.8f, true));
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.drawEllipse (c.x - cr + 0.5f, c.y - cr + 0.5f, cr * 2.0f - 1.0f, cr * 2.0f - 1.0f, 1.0f);

        // pointer
        const float x1 = c.x + std::sin (ang) * cr * 0.35f, y1 = c.y - std::cos (ang) * cr * 0.35f;
        const float x2 = c.x + std::sin (ang) * cr * 0.88f, y2 = c.y - std::cos (ang) * cr * 0.88f;
        g.setColour (col);
        g.drawLine (x1, y1, x2, y2, 2.5f);
    }
};

struct KnobSpec
{
    juce::String id, label;
    std::function<juce::String (float)> fmt;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const KnobSpec& spec, juce::Colour c)
        : label (spec.label), fmt (spec.fmt)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                    juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        addAndMakeVisible (slider);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.id, slider);
        if (auto* p = apvts.getParameter (spec.id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }

    void resized() override
    {
        auto b = getLocalBounds();
        slider.setBounds (b.removeFromTop (juce::jmax (10, getHeight() - 30)));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().removeFromBottom (30);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.setFont (F (13.0f));
        g.drawText (fmt ? fmt ((float) slider.getValue()) : juce::String (slider.getValue(), 2),
                    b.removeFromTop (15), juce::Justification::centred);
        g.setColour (juce::Colour (0xff8a8f9c));
        g.setFont (F (12.0f));
        g.drawText (label, b, juce::Justification::centred);
    }

private:
    juce::String label;
    std::function<juce::String (float)> fmt;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

//==============================================================================
// 3D waterfall: spectrum or waveform history drawn as stacked ribbons in perspective
class Scope3D : public juce::Component, private juce::Timer
{
public:
    enum class Mode { spectrum, wave };

    explicit Scope3D (SubGunProcessor& p) : proc (p)
    {
        startTimerHz (30);
        for (auto& row : hist) row.fill (0.0f);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void setMode (Mode m) { mode = m; for (auto& row : hist) row.fill (0.0f); }

    void mouseDown (const juce::MouseEvent&) override { yawStart = yaw; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        yaw = juce::jlimit (-0.9f, 0.9f, yawStart + (float) e.getDistanceFromDragStartX() * 0.004f);
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { yaw = 0.35f; }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0c0e14), 0, 0,
                                                 juce::Colour (0xff040508), 0, bounds.getBottom(), false));
        g.fillRoundedRectangle (bounds, 6.0f);

        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;  clip.addRoundedRectangle (bounds, 6.0f);
        g.reduceClipRegion (clip);

        const float W = bounds.getWidth(), H = bounds.getHeight();
        const float persp = 1.1f, sMin = 1.0f / (1.0f + persp);
        const float bottom = H * 0.90f, top = H * 0.36f;
        const bool wave = (mode == Mode::wave);
        const float ampScale = wave ? H * 0.20f : H * 0.55f;

        auto proj = [&] (float xn, float z, float yOff)
        {
            const float s  = 1.0f / (1.0f + z * persp);
            const float sn = (s - sMin) / (1.0f - sMin);
            return juce::Point<float> (W * 0.5f + (xn * W * 0.42f + z * yaw * W * 0.5f) * s,
                                       top + (bottom - top) * sn - yOff * s);
        };

        // floor grid
        g.setColour (juce::Colour (0xff3a4a7a).withAlpha (0.18f));
        for (int i = 0; i <= 8; ++i)
        {
            const float z = (float) i / 8.0f;
            g.drawLine ({ proj (-1.0f, z, 0.0f), proj (1.0f, z, 0.0f) }, 1.0f);
        }
        for (int i = 0; i <= 8; ++i)
        {
            const float xn = -1.0f + (float) i * 0.25f;
            g.drawLine ({ proj (xn, 0.0f, 0.0f), proj (xn, 1.0f, 0.0f) }, 1.0f);
        }

        // ribbons, back to front
        const float fillBottom = wave ? -ampScale : 0.0f;
        for (int a = rows - 1; a >= 0; --a)
        {
            const float z = (float) a / (float) (rows - 1);
            const auto& row = hist[(size_t) ((head - 1 - a + rows * 2) % rows)];

            juce::Path line, fill;
            const auto p0 = proj (-1.0f, z, row[0] * ampScale);
            line.startNewSubPath (p0);
            fill.startNewSubPath (proj (-1.0f, z, fillBottom));
            fill.lineTo (p0);
            for (int c = 1; c < cols; ++c)
            {
                const auto p = proj (-1.0f + 2.0f * (float) c / (float) (cols - 1), z, row[(size_t) c] * ampScale);
                line.lineTo (p);  fill.lineTo (p);
            }
            fill.lineTo (proj (1.0f, z, fillBottom));
            fill.closeSubPath();

            g.setColour (juce::Colour (0xff0a0c12).interpolatedWith (juce::Colour (0xff171b2b), 1.0f - z));
            g.fillPath (fill);

            const float s = 1.0f / (1.0f + z * persp);
            const auto col = juce::Colour (0xffffa347).interpolatedWith (juce::Colour (0xff3563ff), juce::jlimit (0.0f, 1.0f, z * 1.15f))
                                 .withAlpha (1.0f - 0.78f * z);
            if (a == 0)
            {
                g.setColour (col.withAlpha (0.18f));
                g.strokePath (line, juce::PathStrokeType (6.0f));
            }
            g.setColour (col);
            g.strokePath (line, juce::PathStrokeType (juce::jmax (0.8f, 1.8f * s)));
        }

        g.setColour (juce::Colour (0xff8a8f9c));
        g.setFont (F (12.0f));
        g.drawText (juce::String (proc.peakDb.load(), 1) + " dB pk", 12, 8, 120, 16, juce::Justification::left);
        g.drawText (wave ? "waveform  -  drag to rotate" : "spectrum  -  drag to rotate",
                    getWidth() - 232, 8, 220, 16, juce::Justification::right);
    }

private:
    static constexpr int rows = 56, cols = 128, fftOrder = 12, fftSize = 1 << fftOrder;

    void timerCallback() override { update(); repaint(); }

    void snapshot (float* dest, int count) const
    {
        const int pos = proc.vizPos.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
            dest[i] = proc.vizBuf[(size_t) ((pos - count + i + SubGunProcessor::vizSize) & SubGunProcessor::vizMask)];
    }

    void update()
    {
        std::array<float, cols> row {};

        if (mode == Mode::wave)
        {
            snapshot (fftData.data(), fftSize);
            const int chunk = fftSize / cols;
            for (int c = 0; c < cols; ++c)
            {
                float best = 0.0f;
                for (int i = 0; i < chunk; ++i)
                {
                    const float v = fftData[(size_t) (c * chunk + i)];
                    if (std::abs (v) > std::abs (best)) best = v;
                }
                row[(size_t) c] = best;
            }
        }
        else
        {
            fftData.fill (0.0f);
            snapshot (fftData.data(), fftSize);
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
            const double binHz = sr / (double) fftSize;
            auto freqAt = [] (int c) { return 20.0 * std::pow (600.0, (double) c / (double) cols); };
            for (int c = 0; c < cols; ++c)
            {
                const int lo = juce::jlimit (1, fftSize / 2 - 2, (int) std::floor (freqAt (c) / binHz));
                const int hi = juce::jlimit (lo + 1, fftSize / 2 - 1, (int) std::ceil (freqAt (c + 1) / binHz));
                float m = 0.0f;
                for (int b = lo; b <= hi; ++b) m = juce::jmax (m, fftData[(size_t) b]);
                float db = juce::Decibels::gainToDecibels (m / (fftSize * 0.25f) + 1.0e-7f);
                db += 4.5f * std::log2 ((float) freqAt (c) / 50.0f);   // gentle tilt so highs are visible
                row[(size_t) c] = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
            }
        }
        hist[(size_t) head] = row;
        head = (head + 1) % rows;
    }

    SubGunProcessor& proc;
    Mode mode = Mode::spectrum;
    std::array<std::array<float, cols>, rows> hist {};
    int head = 0;
    float yaw = 0.35f, yawStart = 0.35f;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
};

//==============================================================================
// One stem: label, mute/solo, extruded 3D envelope ribbon, and its knobs
class StemLane : public juce::Component
{
public:
    enum Kind { Sub, Trans, Body, Spice };

    StemLane (SubGunProcessor& p, Kind k, const juce::String& displayName, const juce::String& stemId_,
              juce::Colour c, const std::vector<KnobSpec>& specs, bool withShape = false)
        : proc (p), kind (k), name (displayName), stemId (stemId_), colour (c)
    {
        for (auto& s : specs)
        {
            knobs.add (new Knob (proc.apvts, s, c));
            addAndMakeVisible (knobs.getLast());
        }

        auto setup = [this] (juce::TextButton& b, const char* text, juce::Colour on)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff23262e));
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb0b5c0));
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            addAndMakeVisible (b);
        };
        setup (mute, "M", juce::Colour (0xffe0605a));
        setup (solo, "S", juce::Colour (0xffe6c229));
        muteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "mute_" + stemId, mute);
        soloAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "solo_" + stemId, solo);

        if (withShape)
        {
            shapeBox.addItemList ({ "exp", "lin", "log" }, 1);
            addAndMakeVisible (shapeBox);
            shapeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "shape", shapeBox);
            hasShape = true;
        }
    }

    void resized() override
    {
        mute.setBounds (14, 60, 30, 24);
        solo.setBounds (50, 60, 30, 24);
        int x = 552;
        for (auto* k : knobs) { k->setBounds (x, 4, 80, getHeight() - 8); x += 82; }
        if (hasShape) shapeBox.setBounds (x + 8, 30, 66, 24);
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0e1015));
        g.fillRoundedRectangle (b, 5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (0.0f, 0.0f, 4.0f, b.getHeight(), 2.0f);

        g.setColour (colour);
        g.setFont (F (17.0f, true));
        g.drawText (name, 14, 10, 130, 22, juce::Justification::left);

        const float db = proc.apvts.getRawParameterValue (stemId)->load();
        g.setColour (juce::Colour (0xff8a8f9c));
        g.setFont (F (13.0f));
        g.drawText (db <= -59.5f ? juce::String ("-inf") : juce::String (db, 1), 14, 32, 100, 18, juce::Justification::left);

        drawRibbon (g, juce::Rectangle<float> (156.0f, 8.0f, 384.0f, b.getHeight() - 16.0f));
    }

private:
    float P (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    float levelAt (float t) const
    {
        const float hold = P ("hold"), rel = juce::jmax (5.0f, P ("release"));
        const float gate = juce::jlimit (0.0f, 1.0f, t > hold ? 1.0f - (t - hold) / rel : 1.0f);
        switch (kind)
        {
            case Sub:   { const float sw = P ("swell"); return (sw > 0.5f ? juce::jmin (1.0f, t / sw) : 1.0f) * gate; }
            case Trans: return sg::shapeCurve (1.0f - t / juce::jmax (1.0f, P ("transDecay")), 0) * gate;
            case Body:  return sg::shapeCurve (1.0f - t / juce::jmax (1.0f, P ("bodyDecay")), (int) P ("shape")) * gate;
            case Spice:
            {
                const float pos = P ("spicePos"), len = juce::jmax (1.0f, P ("spiceLen"));
                if (t < pos || t > pos + len) return 0.0f;
                const float w = std::sin (juce::MathConstants<float>::twoPi * (t - pos) / len);
                return w * w * gate;
            }
        }
        return 0.0f;
    }

    void drawRibbon (juce::Graphics& g, juce::Rectangle<float> vis)
    {
        const bool muted = proc.apvts.getRawParameterValue ("mute_" + stemId)->load() > 0.5f;
        g.setColour (juce::Colour (0xff07080b));
        g.fillRoundedRectangle (vis, 4.0f);

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (vis.toNearestInt());
        if (muted) g.beginTransparencyLayer (0.3f);

        const float T = juce::jlimit (300.0f, 3000.0f, P ("hold") + P ("release"));
        constexpr int N = 160, depth = 7;
        const float padX = 10.0f, padY = 12.0f;
        const float x0 = vis.getX() + padX, w = vis.getWidth() - 2.0f * padX - (float) depth;
        const float base = vis.getBottom() - padY, hgt = vis.getHeight() - 2.0f * padY - (float) depth;

        std::array<float, N> ys {};
        for (int i = 0; i < N; ++i) ys[(size_t) i] = levelAt (T * (float) i / (float) (N - 1));

        auto build = [&] (float dx, float dy, bool closed)
        {
            juce::Path p;
            if (closed) p.startNewSubPath (x0 + dx, base + dy);
            for (int i = 0; i < N; ++i)
            {
                const float x = x0 + w * (float) i / (float) (N - 1) + dx;
                const float y = base - ys[(size_t) i] * hgt + dy;
                if (i == 0 && ! closed) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            if (closed) { p.lineTo (x0 + w + dx, base + dy); p.closeSubPath(); }
            return p;
        };

        for (int k = depth; k >= 1; --k)               // extrusion layers, back to front
        {
            g.setColour (colour.darker (1.8f - 0.2f * (float) (depth - k)).withAlpha (0.9f));
            g.fillPath (build ((float) k, (float) -k, true));
        }
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.85f), 0.0f, vis.getY() + padY,
                                                 colour.withAlpha (0.15f), 0.0f, base, false));
        g.fillPath (build (0.0f, 0.0f, true));
        g.setColour (colour.brighter (0.7f));
        g.strokePath (build (0.0f, 0.0f, false), juce::PathStrokeType (1.6f));

        if (kind == Sub)                               // pitch-dive curve
        {
            const float dive = juce::jmax (1.0f, P ("diveTime"));
            juce::Path pc;
            for (int i = 0; i < N; ++i)
            {
                const float t = T * (float) i / (float) (N - 1);
                const float r = juce::jlimit (0.0f, 1.0f, 1.0f - t / dive);
                const float x = x0 + w * (float) i / (float) (N - 1), y = base - r * r * hgt;
                if (i == 0) pc.startNewSubPath (x, y); else pc.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.strokePath (pc, juce::PathStrokeType (1.2f));
        }

        const float ph = proc.playheadMs.load();
        if (ph >= 0.0f && ph <= T)
        {
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawVerticalLine ((int) (x0 + w * ph / T), vis.getY(), vis.getBottom());
        }
        if (muted) g.endTransparencyLayer();

        g.setColour (juce::Colour (0xff5d6270));
        g.setFont (F (11.0f));
        g.drawText (juce::String ((int) T) + " ms", vis.toNearestInt().reduced (6, 2), juce::Justification::topRight);
    }

    SubGunProcessor& proc;
    Kind kind;
    juce::String name, stemId;
    juce::Colour colour;
    juce::OwnedArray<Knob> knobs;
    juce::TextButton mute, solo;
    juce::ComboBox shapeBox;
    bool hasShape = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAtt, soloAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAtt;
};

//==============================================================================
class PlayButton : public juce::Component
{
public:
    explicit PlayButton (std::atomic<int>& g) : gate (g) {}
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool down = gate.load() != 0;
        g.setGradientFill (juce::ColourGradient (down ? juce::Colour (0xffff9a3c) : juce::Colour (0xff3a3e49), b.getX(), b.getY(),
                                                 down ? juce::Colour (0xffb35a10) : juce::Colour (0xff15171c), b.getX(), b.getBottom(), false));
        g.fillEllipse (b);
        juce::Path tri;
        const auto c = b.getCentre();
        tri.addTriangle (c.x - 6.0f, c.y - 9.0f, c.x - 6.0f, c.y + 9.0f, c.x + 10.0f, c.y);
        g.setColour (down ? juce::Colours::black : juce::Colours::white);
        g.fillPath (tri);
    }
    void mouseDown (const juce::MouseEvent&) override { gate.store (1); repaint(); }
    void mouseUp   (const juce::MouseEvent&) override { gate.store (0); repaint(); }
private:
    std::atomic<int>& gate;
};
} // namespace ui
