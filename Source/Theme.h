#pragma once
#include "Common.h"

// Glossy dark-indigo theme: frosted glass panels, soft sheen, violet -> cyan accents.
namespace th
{
inline juce::Font F (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

inline const juce::Colour bg0 (0xff090814), bg1 (0xff1a1550), text (0xffeeeaff), dim (0xff8f8bb4),
                          violet (0xff8b7bff), cyan (0xff58d8ff),
                          cSub (0xff5aa9ff), cTrans (0xffff7ac6), cBody (0xff45e3c8), cSpice (0xffffd36e);

inline void background (juce::Graphics& g, int w, int h)
{
    const float W = (float) w, H = (float) h;
    g.setGradientFill (juce::ColourGradient (bg1, 0, 0, bg0, W * 0.7f, H, false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (violet.withAlpha (0.22f), W * 0.12f, -H * 0.05f,
                                             violet.withAlpha (0.0f), W * 0.12f, H * 0.55f, true));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (cyan.withAlpha (0.10f), W * 0.95f, H * 1.05f,
                                             cyan.withAlpha (0.0f), W * 0.95f, H * 0.45f, true));
    g.fillAll();
}

// frosted glass: shadow, tinted body, top sheen, gradient hairline border
inline void glass (juce::Graphics& g, juce::Rectangle<float> r, float rad = 14.0f,
                   juce::Colour tint = juce::Colours::white, float a = 1.0f)
{
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.10f * a));
        g.fillRoundedRectangle (r.expanded ((float) i).translated (0.0f, (float) i * 1.5f), rad + (float) i);
    }
    g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.11f * a), 0, r.getY(),
                                             tint.withAlpha (0.035f * a), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, rad);
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;  clip.addRoundedRectangle (r, rad);
        g.reduceClipRegion (clip);
        const auto top = r.withHeight (r.getHeight() * 0.45f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.09f * a), 0, top.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0, top.getBottom(), false));
        g.fillRect (top);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.26f * a), 0, r.getY(),
                                             juce::Colours::white.withAlpha (0.05f * a), 0, r.getBottom(), false));
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
}

// symmetric min/max waveform with a soft glow
inline void peaks (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& pk,
                   juce::Colour c1, juce::Colour c2, float alpha)
{
    const int cols = (int) pk.size() / 2;
    if (cols < 2) return;
    juce::Path p;
    const float cy = r.getCentreY(), hh = r.getHeight() * 0.5f;
    for (int c = 0; c < cols; ++c)
    {
        const float x = r.getX() + r.getWidth() * (float) c / (float) (cols - 1);
        const float y = cy - pk[(size_t) c * 2 + 1] * hh;
        if (c == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    for (int c = cols - 1; c >= 0; --c)
        p.lineTo (r.getX() + r.getWidth() * (float) c / (float) (cols - 1), cy - pk[(size_t) c * 2] * hh);
    p.closeSubPath();

    g.setGradientFill (juce::ColourGradient (c1.withAlpha (alpha * 0.35f), r.getX(), 0, c2.withAlpha (alpha * 0.35f), r.getRight(), 0, false));
    g.strokePath (p, juce::PathStrokeType (5.0f));
    g.setGradientFill (juce::ColourGradient (c1.withAlpha (alpha * 0.55f), r.getX(), 0, c2.withAlpha (alpha * 0.55f), r.getRight(), 0, false));
    g.fillPath (p);
    g.setGradientFill (juce::ColourGradient (c1.withAlpha (alpha), r.getX(), 0, c2.withAlpha (alpha), r.getRight(), 0, false));
    g.strokePath (p, juce::PathStrokeType (1.2f));
}

class GlossLNF : public juce::LookAndFeel_V4
{
public:
    GlossLNF()
    {
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1a1644));
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, violet.withAlpha (0.5f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return F (juce::jmin (15.0f, (float) h * 0.5f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return F (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = b.getToggleState();
        juce::Colour base = on ? b.findColour (juce::TextButton::buttonOnColourId) : juce::Colour (0xff2a2552);
        if (down) base = base.darker (0.25f); else if (hi) base = base.brighter (0.12f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.3f), 0, r.getY(), base.darker (0.4f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (r, rad);
        auto sheen = r.reduced (1.5f, 1.0f);
        sheen = sheen.withHeight (sheen.getHeight() * 0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), 0, sheen.getY(),
                                                 juce::Colours::white.withAlpha (0.0f), 0, sheen.getBottom(), false));
        g.fillRoundedRectangle (sheen, rad);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawRoundedRectangle (r, rad, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), 0, 0, juce::Colours::white.withAlpha (0.05f), 0, (float) h, false));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.2f));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        juce::Path c;
        const float cx = (float) w - 16.0f, cy = (float) h * 0.5f;
        c.startNewSubPath (cx - 4, cy - 2); c.lineTo (cx, cy + 2); c.lineTo (cx + 4, cy - 2);
        g.setColour (dim);
        g.strokePath (c, juce::PathStrokeType (1.6f));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        const juce::Rectangle<float> track ((float) x, cy - 3.0f, (float) w, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.fillRoundedRectangle (track, 3.0f);
        auto fill = track.withRight (pos);
        g.setGradientFill (juce::ColourGradient (violet, fill.getX(), 0, cyan, track.getRight(), 0, false));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillEllipse (pos - 9.0f, cy - 6.0f, 18.0f, 18.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, pos, cy - 9.0f, juce::Colour (0xffb9b0ff), pos, cy + 9.0f, false));
        g.fillEllipse (pos - 9.0f, cy - 9.0f, 18.0f, 18.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (7.0f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);

        juce::Path track, val;
        track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        val.addCentredArc   (c.x, c.y, r, r, 0.0f, startAngle, ang, true);
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col.withAlpha (0.22f));
        g.strokePath (val, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (col);
        g.strokePath (val, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float cr = r - 6.0f;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillEllipse (c.x - cr + 1.0f, c.y - cr + 3.0f, cr * 2.0f, cr * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a5690), c.x, c.y - cr, juce::Colour (0xff15122e), c.x, c.y + cr, false));
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
        {
            juce::Graphics::ScopedSaveState ss (g);
            juce::Path clip;  clip.addEllipse (c.x - cr, c.y - cr, cr * 2.0f, cr * 2.0f);
            g.reduceClipRegion (clip);
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.38f), c.x, c.y - cr,
                                                     juce::Colours::white.withAlpha (0.0f), c.x, c.y + cr * 0.1f, false));
            g.fillEllipse (c.x - cr * 0.8f, c.y - cr * 0.95f, cr * 1.6f, cr * 1.1f);
        }
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.drawEllipse (c.x - cr + 0.5f, c.y - cr + 0.5f, cr * 2.0f - 1.0f, cr * 2.0f - 1.0f, 1.0f);

        g.setColour (juce::Colours::white);
        g.drawLine (c.x + std::sin (ang) * cr * 0.4f, c.y - std::cos (ang) * cr * 0.4f,
                    c.x + std::sin (ang) * cr * 0.86f, c.y - std::cos (ang) * cr * 0.86f, 2.4f);
    }
};
} // namespace th
