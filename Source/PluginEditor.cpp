#include "PluginEditor.h"

namespace
{
using ui::KnobSpec;
const auto dbF  = [] (float v) { return v <= -59.5f ? juce::String ("-inf") : (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
const auto msF  = [] (float v) { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; };
const auto stF  = [] (float v) { return juce::String (v, 1) + " st"; };
const auto numF = [] (float v) { return juce::String (v, 2); };
const auto hzF  = [] (float v) { return juce::String ((int) v) + " Hz"; };
const auto hzF1 = [] (float v) { return juce::String (v, 1) + " Hz"; };
}

SubGunEditor::SubGunEditor (SubGunProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), scope (p),
      note (*p.apvts.getParameter ("root")), play (p.uiGate), meter (p)
{
    setLookAndFeel (&lnf);
    setSize (1120, 700);

    // ---- header
    for (auto* b : { &tabMatch, &tabEngine, &tabFx })
    {
        b->setRadioGroupId (7);
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff7d6bff));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setColour (juce::TextButton::textColourOffId, th::dim);
        addAndMakeVisible (b);
    }
    tabMatch.setToggleState (true, juce::dontSendNotification);
    tabMatch.onClick  = [this] { showTab (0); };
    tabEngine.onClick = [this] { showTab (1); };
    tabFx.onClick     = [this] { showTab (2); };
    addAndMakeVisible (note);
    addAndMakeVisible (play);
    addAndMakeVisible (meter);
    addAndMakeVisible (matchPage);  addAndMakeVisible (enginePage);  addAndMakeVisible (fxPage);

    // ---- match page
    matchPage.addAndMakeVisible (wave);
    wave.onFile = [this] (juce::File f) { loadSample (f); };
    wave.setBusy (false, 0.0f, "");

    matchPage.addAndMakeVisible (scope);
    specBtn.setRadioGroupId (1);  waveBtn.setRadioGroupId (1);
    specBtn.setClickingTogglesState (true);  waveBtn.setClickingTogglesState (true);
    specBtn.setToggleState (true, juce::dontSendNotification);
    specBtn.onClick = [this] { scope.setMode (ui::Scope3D::Mode::spectrum); };
    waveBtn.onClick = [this] { scope.setMode (ui::Scope3D::Mode::wave); };
    for (auto* b : { &specBtn, &waveBtn })
    {
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4a8dff));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setColour (juce::TextButton::textColourOffId, th::dim);
        matchPage.addAndMakeVisible (b);
    }

    for (int i = 0; i < 8; ++i)
    {
        auto* c = new ui::VariationCard (i);
        c->onClick = [this] (int idx) { choose (idx); };
        cards.add (c);
        matchPage.addAndMakeVisible (c);
    }

    backBtn.onClick = [this]
    {
        if (history.empty()) return;
        current = history.back();  history.pop_back();
        selected = -1;
        refreshCards();
    };
    evolveBtn.onClick = [this] { evolveFrom (selected >= 0 ? selected : 0); };
    autoBtn.setClickingTogglesState (true);
    autoBtn.setToggleState (true, juce::dontSendNotification);
    for (auto* b : { &backBtn, &evolveBtn, &autoBtn })
    {
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff7d6bff));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->setColour (juce::TextButton::textColourOffId, th::text);
        matchPage.addAndMakeVisible (b);
    }
    spreadSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    spreadSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    spreadSlider.setRange (0.0, 1.0);
    spreadSlider.setValue (0.35);
    matchPage.addAndMakeVisible (spreadSlider);
    spreadLabel.setText ("variation spread", juce::dontSendNotification);
    spreadLabel.setColour (juce::Label::textColourId, th::dim);
    spreadLabel.setFont (th::F (12.0f));
    matchPage.addAndMakeVisible (spreadLabel);
    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, th::dim);
    infoLabel.setFont (th::F (12.0f));
    matchPage.addAndMakeVisible (infoLabel);

    // ---- engine page
    lanes.add (new ui::StemLane (proc, ui::StemLane::Sub, "sub", "sub", th::cSub,
        { { "sub", "level", dbF }, { "diveAmt", "dive", stF }, { "diveTime", "dive time", msF },
          { "swell", "swell", msF }, { "subDrive", "drive", dbF }, { "hold", "hold", msF } }));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Trans, "transient", "trans", th::cTrans,
        { { "trans", "level", dbF }, { "transDecay", "decay", msF } }));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Body, "body", "body", th::cBody,
        { { "body", "level", dbF }, { "bodyDecay", "decay", msF }, { "bodyDrive", "drive", dbF }, { "crunch", "crunch", numF } }, true));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Spice, "spice", "spice", th::cSpice,
        { { "spice", "level", dbF }, { "spicePos", "position", msF }, { "spiceLen", "length", msF }, { "spiceTone", "tone", hzF } }));
    for (auto* l : lanes) enginePage.addAndMakeVisible (l);

    for (auto& s : std::vector<KnobSpec> { { "pitch", "pitch", hzF1 }, { "release", "release", msF },
                                           { "high", "high", dbF }, { "vol", "volume", dbF } })
    {
        masterKnobs.add (new ui::Knob (proc.apvts, s, th::text));
        enginePage.addAndMakeVisible (masterKnobs.getLast());
    }

    showTab (0);
    startTimerHz (30);
}

SubGunEditor::~SubGunEditor() { setLookAndFeel (nullptr); }

//==============================================================================
void SubGunEditor::showTab (int i)
{
    currentTab = i;
    matchPage.setVisible (i == 0);
    enginePage.setVisible (i == 1);
    fxPage.setVisible (i == 2);
    repaint();
}

bool SubGunEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void SubGunEditor::filesDropped (const juce::StringArray& files, int, int)
{
    wave.setHover (false);
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { loadSample (juce::File (f)); break; }
}

void SubGunEditor::loadSample (const juce::File& f)
{
    tabMatch.setToggleState (true, juce::dontSendNotification);
    showTab (0);
    history.clear();  current = Batch();  selected = -1;
    refreshCards();
    proc.matcher.startFit (f, (float) spreadSlider.getValue());
}

void SubGunEditor::refreshCards()
{
    for (int i = 0; i < cards.size(); ++i)
    {
        if (i < (int) current.items.size())
            cards[i]->set (&current.items[(size_t) i], &targetPeaks, i == selected);
        else
            cards[i]->set (nullptr, nullptr, false);
    }
    backBtn.setEnabled (! history.empty());
}

void SubGunEditor::choose (int index)
{
    if (index < 0 || index >= (int) current.items.size()) return;
    selected = index;
    const auto& v = current.items[(size_t) index];
    proc.applyParams (v.p);
    proc.trigger();
    wave.setOverlay (v.peaks);
    refreshCards();
    if (autoBtn.getToggleState() && ! proc.matcher.isBusy()) evolveFrom (index);
}

void SubGunEditor::evolveFrom (int index)
{
    if (index < 0 || index >= (int) current.items.size() || proc.matcher.isBusy()) return;
    proc.matcher.startEvolve (current.items[(size_t) index].p, (float) spreadSlider.getValue());
}

void SubGunEditor::timerCallback()
{
    meter.repaint();
    if (currentTab == 1) for (auto* l : lanes) l->repaint();

    const bool busy = proc.matcher.isBusy();
    wave.setBusy (busy, proc.matcher.progress.load(), proc.matcher.getStatus());
    evolveBtn.setEnabled (! busy && ! current.items.empty());

    const int v = proc.matcher.version.load();
    if (v != lastVersion)
    {
        lastVersion = v;
        const auto snap = proc.matcher.snapshot();
        targetPeaks = snap.targetPeaks;
        wave.setTarget (snap.targetPeaks, snap.name, snap.seconds);

        if (! snap.batch.items.empty() && snap.batch.id != current.id)
        {
            const bool firstOfSample = current.items.empty();
            if (! current.items.empty()) history.push_back (current);
            current = snap.batch;
            selected = 0;
            wave.setOverlay (current.items[0].peaks);
            if (firstOfSample) { proc.applyParams (current.items[0].p); proc.trigger(); }
            refreshCards();
        }
    }
    if (! current.items.empty())
        infoLabel.setText ("generation " + juce::String (current.generation) + "   -   recreation match "
                           + juce::String ((int) std::lround (current.items[0].match)) + "%",
                           juce::dontSendNotification);
    else
        infoLabel.setText ("", juce::dontSendNotification);
}

//==============================================================================
void SubGunEditor::paint (juce::Graphics& g)
{
    th::background (g, getWidth(), getHeight());

    // logo: widely tracked, gradient lettering with a soft glow
    const char* letters = "SUBGUN";
    for (int i = 0; i < 6; ++i)
    {
        const juce::Rectangle<int> cell (28 + i * 25, 14, 28, 36);
        g.setFont (th::F (27.0f, true));
        g.setColour (th::violet.withAlpha (0.28f));
        g.drawText (juce::String::charToString (letters[i]), cell.translated (0, 1), juce::Justification::centred);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0, 14.0f, juce::Colour (0xff9d8dff), 0, 50.0f, false));
        g.drawText (juce::String::charToString (letters[i]), cell, juce::Justification::centred);
    }

    if (currentTab == 1)
    {
        th::glass (g, masterStrip.toFloat(), 14.0f, juce::Colours::white, 0.8f);
        g.setColour (th::text);
        g.setFont (th::F (17.0f, true));
        g.drawText ("master", masterStrip.getX() + 22, masterStrip.getY() + 12, 120, 22, juce::Justification::left);
        g.setColour (th::dim);
        g.setFont (th::F (12.0f));
        g.drawText ("pitch = the pitch of the recreated sample at the root key", masterStrip.getX() + 22, masterStrip.getY() + 36, 220, 36, juce::Justification::topLeft);
    }
    else if (currentTab == 0)
    {
        th::glass (g, controlsBar.toFloat(), 14.0f, juce::Colours::white, 0.8f);
        th::glass (g, juce::Rectangle<float> (16.0f + 742.0f, 72.0f, 346.0f, 238.0f), 16.0f, th::cyan, 0.9f);
    }
    else
    {
        // FX rack placeholder, ready for the next milestone
        const char* names[4] = { "Distortion", "Filter", "Multiband", "Chorus / Width" };
        const char* hints[4] = { "soft clip, fold, bitcrush", "LP / HP / formant, resonance", "OTT-style compression", "stereo spread for the top end" };
        const int cw = (getWidth() - 32 - 3 * 12) / 4;
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> c ((float) (16 + i * (cw + 12)), 72.0f, (float) cw, 260.0f);
            th::glass (g, c, 16.0f, juce::Colour (0xffff7ac6), 0.8f);
            g.setColour (juce::Colour (0xffff7ac6).withAlpha (0.8f));
            g.fillEllipse (c.getX() + 20.0f, c.getY() + 22.0f, 9.0f, 9.0f);
            g.setColour (th::text);
            g.setFont (th::F (18.0f, true));
            g.drawText (names[i], (int) c.getX() + 40, (int) c.getY() + 14, cw - 60, 26, juce::Justification::left);
            g.setColour (th::dim);
            g.setFont (th::F (13.0f));
            g.drawText (hints[i], (int) c.getX() + 20, (int) c.getY() + 50, cw - 40, 40, juce::Justification::topLeft);
            g.setColour (th::dim.withAlpha (0.7f));
            g.drawText ("coming soon", c.toNearestInt().withTrimmedTop (200), juce::Justification::centred);
        }
    }
}

void SubGunEditor::resized()
{
    auto r = getLocalBounds();
    // header
    tabMatch.setBounds  (getWidth() / 2 - 156, 18, 100, 30);
    tabEngine.setBounds (getWidth() / 2 - 50,  18, 100, 30);
    tabFx.setBounds     (getWidth() / 2 + 56,  18, 100, 30);
    meter.setBounds (790, 28, 150, 10);
    note.setBounds (958, 18, 84, 30);
    play.setBounds (getWidth() - 74, 10, 48, 48);

    auto content = r.withTrimmedTop (72).reduced (16, 0).withTrimmedBottom (16);
    matchPage.setBounds (content);  enginePage.setBounds (content);  fxPage.setBounds (content);

    // match page (local coordinates)
    const int W = content.getWidth();
    wave.setBounds (0, 0, 730, 238);
    scope.setBounds (742 + (346 - 200) / 2, 4, 200, 200);
    specBtn.setBounds (742 + 60, 238 - 34, 110, 26);
    waveBtn.setBounds (742 + 176, 238 - 34, 110, 26);

    const int cw = (W - 3 * 12) / 4, ch = 134;
    for (int i = 0; i < cards.size(); ++i)
        cards[i]->setBounds ((i % 4) * (cw + 12), 250 + (i / 4) * (ch + 12), cw, ch);

    const int by = 250 + 2 * (ch + 12);
    controlsBar = juce::Rectangle<int> (content.getX(), content.getY() + by, W, 56);
    backBtn.setBounds (14, by + 13, 80, 30);
    evolveBtn.setBounds (102, by + 13, 110, 30);
    autoBtn.setBounds (220, by + 13, 124, 30);
    spreadLabel.setBounds (366, by + 6, 160, 16);
    spreadSlider.setBounds (366, by + 24, 190, 24);
    infoLabel.setBounds (580, by + 13, W - 580 - 18, 30);

    // engine page
    const int lh = 104;
    for (int i = 0; i < lanes.size(); ++i) lanes[i]->setBounds (0, i * (lh + 8), W, lh);
    const int my = lanes.size() * (lh + 8);
    masterStrip = juce::Rectangle<int> (content.getX(), content.getY() + my, W, 100);
    int x = 300;
    for (auto* k : masterKnobs) { k->setBounds (x, my + 2, 90, 96); x += 96; }
}
