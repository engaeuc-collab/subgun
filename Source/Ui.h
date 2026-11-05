#pragma once
#include "PluginProcessor.h"
#include "Theme.h"

namespace ui
{
using th::F;

//==============================================================================
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
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setColour (juce::Slider::rotarySliderFillColourId, c);
        addAndMakeVisible (slider);
        att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.id, slider);
        if (auto* p = apvts.getParameter (spec.id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.onValueChange = [this] { repaint(); };
    }

    void resized() override { slider.setBounds (getLocalBounds().withTrimmedBottom (32)); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().removeFromBottom (32);
        g.setColour (th::text);
        g.setFont (F (13.0f));
        g.drawText (fmt ? fmt ((float) slider.getValue()) : juce::String (slider.getValue(), 2), b.removeFromTop (16), juce::Justification::centred);
        g.setColour (th::dim);
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
// Perspective waterfall of the live output (spectrum or waveform), drawn in a glowing ring
class Scope3D : public juce::Component, private juce::Timer
{
public:
    enum class Mode { spectrum, wave };

    explicit Scope3D (SubGunProcessor& p) : proc (p)
    {
        startTimerHz (30);
        for (auto& row : hist) row.fill (0.0f);
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
        const auto bounds = getLocalBounds().toFloat().reduced (3.0f);
        const float W = bounds.getWidth(), H = bounds.getHeight();

        juce::Path circle;  circle.addEllipse (bounds);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (circle);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c1650), bounds.getCentreX(), bounds.getY(),
                                                     juce::Colour (0xff05040d), bounds.getCentreX(), bounds.getBottom(), false));
            g.fillEllipse (bounds);
            g.addTransform (juce::AffineTransform::translation (bounds.getX(), bounds.getY()));

            const float persp = 1.1f, sMin = 1.0f / (1.0f + persp);
            const float bottom = H * 0.86f, top = H * 0.40f;
            const bool wave = (mode == Mode::wave);
            const float ampScale = wave ? H * 0.18f : H * 0.50f;

            auto proj = [&] (float xn, float z, float yOff)
            {
                const float s  = 1.0f / (1.0f + z * persp);
                const float sn = (s - sMin) / (1.0f - sMin);
                return juce::Point<float> (W * 0.5f + (xn * W * 0.46f + z * yaw * W * 0.5f) * s,
                                           top + (bottom - top) * sn - yOff * s);
            };

            g.setColour (th::violet.withAlpha (0.20f));
            for (int i = 0; i <= 8; ++i)
                g.drawLine ({ proj (-1.0f, (float) i / 8.0f, 0.0f), proj (1.0f, (float) i / 8.0f, 0.0f) }, 1.0f);
            for (int i = 0; i <= 8; ++i)
                g.drawLine ({ proj (-1.0f + (float) i * 0.25f, 0.0f, 0.0f), proj (-1.0f + (float) i * 0.25f, 1.0f, 0.0f) }, 1.0f);

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

                g.setColour (juce::Colour (0xff0b0922).interpolatedWith (juce::Colour (0xff241c66), 1.0f - z).withAlpha (0.96f));
                g.fillPath (fill);

                const float s = 1.0f / (1.0f + z * persp);
                const auto col = th::cyan.interpolatedWith (juce::Colour (0xff7a52ff), juce::jlimit (0.0f, 1.0f, z * 1.2f))
                                     .withAlpha (1.0f - 0.8f * z);
                if (a == 0) { g.setColour (col.withAlpha (0.2f)); g.strokePath (line, juce::PathStrokeType (6.0f)); }
                g.setColour (col);
                g.strokePath (line, juce::PathStrokeType (juce::jmax (0.8f, 1.8f * s)));
            }
        }
        // glass ring + reflection
        g.setGradientFill (juce::ColourGradient (th::cyan.withAlpha (0.9f), bounds.getX(), bounds.getY(),
                                                 th::violet.withAlpha (0.9f), bounds.getRight(), bounds.getBottom(), false));
        g.drawEllipse (bounds, 2.5f);
        g.setColour (th::violet.withAlpha (0.18f));
        g.drawEllipse (bounds.expanded (3.0f), 4.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (circle);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.12f), bounds.getCentreX(), bounds.getY(),
                                                     juce::Colours::white.withAlpha (0.0f), bounds.getCentreX(), bounds.getCentreY(), false));
            g.fillEllipse (bounds.getX() + W * 0.12f, bounds.getY() + 2.0f, W * 0.76f, H * 0.48f);
        }
        g.setColour (th::dim);
        g.setFont (F (11.0f));
        g.drawText (juce::String (proc.peakDb.load(), 1) + " dB", getLocalBounds().removeFromBottom (22), juce::Justification::centred);
    }

private:
    static constexpr int rows = 52, cols = 112, fftOrder = 12, fftSize = 1 << fftOrder;

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
                db += 4.5f * std::log2 ((float) freqAt (c) / 50.0f);
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
// Stem lane: glass card, extruded 3D envelope ribbon, mute/solo, knobs
class StemLane : public juce::Component
{
public:
    enum Kind { Sub, Trans, Body, Spice };

    StemLane (SubGunProcessor& p, Kind k, const juce::String& displayName, const juce::String& stemId_,
              juce::Colour c, const std::vector<KnobSpec>& specs, bool withShape = false)
        : proc (p), kind (k), name (displayName), stemId (stemId_), colour (c)
    {
        for (auto& s : specs) { knobs.add (new Knob (proc.apvts, s, c)); addAndMakeVisible (knobs.getLast()); }

        auto setup = [this] (juce::TextButton& b, const char* text, juce::Colour on)
        {
            b.setButtonText (text);
            b.setClickingTogglesState (true);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setColour (juce::TextButton::textColourOffId, th::dim);
            b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
            addAndMakeVisible (b);
        };
        setup (mute, "M", juce::Colour (0xffff7a8a));
        setup (solo, "S", juce::Colour (0xffffd36e));
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
        mute.setBounds (20, getHeight() - 38, 32, 24);
        solo.setBounds (58, getHeight() - 38, 32, 24);
        int x = 560;
        for (auto* k : knobs) { k->setBounds (x, 6, 80, getHeight() - 12); x += 82; }
        if (hasShape) shapeBox.setBounds (x + 8, getHeight() / 2 - 22, 68, 26);
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        th::glass (g, b.reduced (1.0f), 14.0f, colour, 0.8f);

        g.setColour (colour);
        g.fillEllipse (20.0f, 18.0f, 9.0f, 9.0f);
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (17.0f, 15.0f, 15.0f, 15.0f);
        g.setColour (th::text);
        g.setFont (F (17.0f, true));
        g.drawText (name, 38, 11, 120, 22, juce::Justification::left);

        const float db = proc.apvts.getRawParameterValue (stemId)->load();
        g.setColour (th::dim);
        g.setFont (F (13.0f));
        g.drawText (db <= -59.5f ? juce::String ("-inf dB") : juce::String (db, 1) + " dB", 20, 38, 120, 18, juce::Justification::left);

        drawRibbon (g, juce::Rectangle<float> (166.0f, 10.0f, 380.0f, b.getHeight() - 20.0f));
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
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillRoundedRectangle (vis, 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (vis.reduced (0.5f), 10.0f, 1.0f);

        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;  clip.addRoundedRectangle (vis, 10.0f);
        g.reduceClipRegion (clip);
        if (muted) g.beginTransparencyLayer (0.3f);

        const float T = juce::jlimit (300.0f, 3000.0f, P ("hold") + P ("release"));
        constexpr int N = 160, depth = 7;
        const float padX = 12.0f, padY = 14.0f;
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

        for (int k = depth; k >= 1; --k)
        {
            g.setColour (colour.darker (1.9f - 0.2f * (float) (depth - k)).withAlpha (0.85f));
            g.fillPath (build ((float) k, (float) -k, true));
        }
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.9f), 0.0f, vis.getY() + padY, colour.withAlpha (0.12f), 0.0f, base, false));
        g.fillPath (build (0.0f, 0.0f, true));
        g.setColour (colour.withAlpha (0.3f));
        g.strokePath (build (0.0f, 0.0f, false), juce::PathStrokeType (5.0f));
        g.setColour (colour.brighter (0.8f));
        g.strokePath (build (0.0f, 0.0f, false), juce::PathStrokeType (1.5f));

        if (kind == Sub)
        {
            const float dive = juce::jmax (1.0f, P ("diveTime"));
            juce::Path pc;
            for (int i = 0; i < N; ++i)
            {
                const float r = juce::jlimit (0.0f, 1.0f, 1.0f - T * (float) i / (float) (N - 1) / dive);
                const float x = x0 + w * (float) i / (float) (N - 1), y = base - r * r * hgt;
                if (i == 0) pc.startNewSubPath (x, y); else pc.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.strokePath (pc, juce::PathStrokeType (1.2f));
        }

        const float ph = proc.playheadMs.load();
        if (ph >= 0.0f && ph <= T)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawVerticalLine ((int) (x0 + w * ph / T), vis.getY(), vis.getBottom());
        }
        if (muted) g.endTransparencyLayer();

        g.setColour (th::dim.withAlpha (0.8f));
        g.setFont (F (11.0f));
        g.drawText (juce::String ((int) T) + " ms", vis.toNearestInt().reduced (8, 3), juce::Justification::topRight);
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
        const auto b = getLocalBounds().toFloat().reduced (3.0f);
        const bool down = gate.load() != 0;
        g.setColour (th::violet.withAlpha (down ? 0.5f : 0.22f));
        g.fillEllipse (b.expanded (3.0f));
        g.setGradientFill (juce::ColourGradient (down ? th::cyan : juce::Colour (0xff9d8dff), 0, b.getY(),
                                                 down ? juce::Colour (0xff3a7bff) : juce::Colour (0xff4a35c9), 0, b.getBottom(), false));
        g.fillEllipse (b);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.4f), 0, b.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0, b.getCentreY(), false));
        g.fillEllipse (b.reduced (3.0f, 1.0f).withHeight (b.getHeight() * 0.5f));
        juce::Path tri;
        const auto c = b.getCentre();
        tri.addTriangle (c.x - 5.0f, c.y - 8.0f, c.x - 5.0f, c.y + 8.0f, c.x + 9.0f, c.y);
        g.setColour (juce::Colours::white);
        g.fillPath (tri);
    }
    void mouseDown (const juce::MouseEvent&) override { gate.store (1); repaint(); }
    void mouseUp   (const juce::MouseEvent&) override { gate.store (0); repaint(); }
private:
    std::atomic<int>& gate;
};

// drag up/down to change the root key (FL-style names: 36 = C2)
class NoteBox : public juce::Component
{
public:
    explicit NoteBox (juce::RangedAudioParameter& p)
        : att (p, [this] (float v) { value = (int) std::lround (v); repaint(); })
    {
        att.sendInitialUpdate();
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), 0, 0, juce::Colours::white.withAlpha (0.05f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (th::dim);
        g.setFont (F (11.0f));
        g.drawText ("KEY", getLocalBounds().withTrimmedLeft (12), juce::Justification::centredLeft);
        g.setColour (th::text);
        g.setFont (F (15.0f, true));
        g.drawText (juce::MidiMessage::getMidiNoteName (value, true, true, 5), getLocalBounds().withTrimmedRight (12), juce::Justification::centredRight);
    }
    void mouseDown (const juce::MouseEvent&) override { att.beginGesture(); dragStart = value; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        att.setValueAsPartOfGesture ((float) juce::jlimit (12, 60, dragStart - e.getDistanceFromDragStartY() / 6));
    }
    void mouseUp (const juce::MouseEvent&) override { att.endGesture(); }
private:
    int value = 36, dragStart = 36;
    juce::ParameterAttachment att;
};

class Meter : public juce::Component
{
public:
    explicit Meter (SubGunProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.0f, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        const float f = juce::jlimit (0.0f, 1.0f, (proc.peakDb.load() + 60.0f) / 60.0f);
        if (f > 0.01f)
        {
            auto fill = r.withWidth (r.getWidth() * f);
            g.setGradientFill (juce::ColourGradient (th::violet, r.getX(), 0, th::cyan, r.getRight(), 0, false));
            g.fillRoundedRectangle (fill, r.getHeight() * 0.5f);
        }
    }
private:
    SubGunProcessor& proc;
};

//==============================================================================
// The dropped sample, drawn big and glowing; doubles as the drop zone and progress display
class WaveView : public juce::Component
{
public:
    std::function<void (juce::File)> onFile;

    void setTarget (const std::vector<float>& pk, const juce::String& n, float sec) { target = pk; name = n; seconds = sec; repaint(); }
    void setOverlay (const std::vector<float>& pk) { overlay = pk; repaint(); }
    void setBusy (bool b, float prog, const juce::String& text) { busy = b; progress = prog; status = text; repaint(); }
    void setHover (bool h) { hover = h; repaint(); }

    void mouseUp (const juce::MouseEvent&) override
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a bass sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              { const auto f = fc.getResult(); if (f.existsAsFile() && onFile) onFile (f); });
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        th::glass (g, b.reduced (1.0f), 16.0f, th::violet, 1.0f);
        if (hover)
        {
            g.setColour (th::cyan.withAlpha (0.8f));
            g.drawRoundedRectangle (b.reduced (2.0f), 16.0f, 2.0f);
        }
        const auto wv = b.reduced (26.0f, 44.0f);

        if (target.empty())
        {
            const auto c = b.getCentre().translated (0.0f, -22.0f);
            g.setColour (th::violet.withAlpha (0.25f));
            g.fillEllipse (c.x - 34.0f, c.y - 34.0f, 68.0f, 68.0f);
            g.setGradientFill (juce::ColourGradient (th::cyan, c.x, c.y - 28.0f, th::violet, c.x, c.y + 28.0f, false));
            g.drawEllipse (c.x - 28.0f, c.y - 28.0f, 56.0f, 56.0f, 2.0f);
            juce::Path plus;
            plus.addRectangle (c.x - 1.5f, c.y - 12.0f, 3.0f, 24.0f);
            plus.addRectangle (c.x - 12.0f, c.y - 1.5f, 24.0f, 3.0f);
            g.fillPath (plus);
            g.setColour (th::text);
            g.setFont (F (20.0f, true));
            g.drawText ("Drop a bass sample here", getLocalBounds().withTrimmedTop ((int) c.y + 44).withHeight (30), juce::Justification::centred);
            g.setColour (th::dim);
            g.setFont (F (13.0f));
            g.drawText ("WAV / AIFF / FLAC - or click to browse", getLocalBounds().withTrimmedTop ((int) c.y + 74).withHeight (24), juce::Justification::centred);
        }
        else
        {
            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.drawHorizontalLine ((int) wv.getCentreY(), wv.getX(), wv.getRight());
            th::peaks (g, wv, target, th::violet, juce::Colour (0xffc7b8ff), 0.9f);
            if (! overlay.empty()) th::peaks (g, wv, overlay, th::cyan, juce::Colour (0xff7ee8ff), 0.75f);

            g.setColour (th::text);
            g.setFont (F (15.0f, true));
            g.drawText (name, 26, 14, getWidth() - 200, 22, juce::Justification::left);
            g.setColour (th::dim);
            g.setFont (F (12.0f));
            g.drawText (juce::String (seconds, 2) + " s  -  original in violet, selected variation in cyan",
                        26, 34, getWidth() - 52, 16, juce::Justification::left);
        }

        // status + progress
        const auto bar = juce::Rectangle<float> (26.0f, b.getBottom() - 26.0f, b.getWidth() - 52.0f, 5.0f);
        if (busy)
        {
            g.setColour (juce::Colours::white.withAlpha (0.1f));
            g.fillRoundedRectangle (bar, 2.5f);
            g.setGradientFill (juce::ColourGradient (th::violet, bar.getX(), 0, th::cyan, bar.getRight(), 0, false));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (6.0f, bar.getWidth() * progress)), 2.5f);
        }
        if (busy || ! target.empty())
        {
            g.setColour (busy ? th::text : th::dim);
            g.setFont (F (12.0f));
            g.drawText (status, 26, (int) b.getBottom() - 46, getWidth() - 52, 16, juce::Justification::left);
        }
    }

private:
    std::vector<float> target, overlay;
    juce::String name, status;
    float seconds = 0.0f, progress = 0.0f;
    bool busy = false, hover = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

class VariationCard : public juce::Component
{
public:
    std::function<void (int)> onClick;

    explicit VariationCard (int i) : index (i) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void set (const Variation* v, const std::vector<float>* tgt, bool sel)
    {
        var = v;  target = tgt;  selected = sel;
        repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { if (var != nullptr && getLocalBounds().contains (e.getPosition()) && onClick) onClick (index); }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        th::glass (g, b.reduced (1.0f), 14.0f, selected ? th::violet : juce::Colours::white, selected ? 1.6f : (hover ? 1.2f : 0.9f));
        if (selected)
        {
            g.setColour (th::cyan.withAlpha (0.25f));
            g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 4.0f);
            g.setGradientFill (juce::ColourGradient (th::cyan, b.getX(), b.getY(), th::violet, b.getRight(), b.getBottom(), false));
            g.drawRoundedRectangle (b.reduced (1.5f), 14.0f, 1.8f);
        }
        if (var == nullptr) return;

        const auto wv = juce::Rectangle<float> (b.getX() + 12.0f, b.getY() + 34.0f, b.getWidth() - 24.0f, b.getHeight() - 46.0f);
        if (target != nullptr && ! target->empty())
            th::peaks (g, wv, *target, juce::Colours::white, juce::Colours::white, 0.22f);
        th::peaks (g, wv, var->peaks, selected ? th::cyan : th::violet, selected ? juce::Colour (0xff7ee8ff) : juce::Colour (0xffc7b8ff), 0.95f);

        g.setColour (th::text);
        g.setFont (F (15.0f, true));
        g.drawText (var->name, 14, 8, getWidth() - 80, 22, juce::Justification::left);
        g.setColour (selected ? th::cyan : th::dim);
        g.setFont (F (12.0f));
        g.drawText (juce::String ((int) std::lround (var->match)) + "%", getWidth() - 62, 8, 48, 22, juce::Justification::right);
    }

private:
    int index;
    const Variation* var = nullptr;
    const std::vector<float>* target = nullptr;
    bool selected = false, hover = false;
};
} // namespace ui
