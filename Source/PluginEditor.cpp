#include "PluginEditor.h"

namespace
{
using ui::KnobSpec;
const auto dbF   = [] (float v) { return v <= -59.5f ? juce::String ("-inf") : (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
const auto msF   = [] (float v) { return juce::String (v, v < 100.0f ? 1 : 0) + " ms"; };
const auto stF   = [] (float v) { return juce::String (v, 1) + " st"; };
const auto numF  = [] (float v) { return juce::String (v, 2); };
const auto hzF   = [] (float v) { return juce::String ((int) v) + " Hz"; };

const juce::Colour kSub (0xff4db8ff), kTrans (0xffffa51f), kBody (0xff3ee0a8), kSpice (0xfff2e03a), kMaster (0xffc8ccd6);
}

SubGunEditor::SubGunEditor (SubGunProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), scope (p), playBtn (p.uiGate)
{
    setLookAndFeel (&lnf);
    setSize (1120, 790);

    addAndMakeVisible (scope);

    lanes.add (new ui::StemLane (proc, ui::StemLane::Sub, "sub", "sub", kSub,
        { { "sub", "sub", dbF }, { "diveAmt", "dive", stF }, { "diveTime", "dive time", msF },
          { "swell", "swell", msF }, { "subDrive", "drive", dbF }, { "hold", "hold", msF } }));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Trans, "transient", "trans", kTrans,
        { { "trans", "transient", dbF }, { "transDecay", "decay", msF } }));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Body, "body", "body", kBody,
        { { "body", "body", dbF }, { "bodyDecay", "decay", msF }, { "bodyDrive", "drive", dbF }, { "crunch", "crunch", numF } },
        true));
    lanes.add (new ui::StemLane (proc, ui::StemLane::Spice, "spice", "spice", kSpice,
        { { "spice", "spice", dbF }, { "spicePos", "position", msF }, { "spiceLen", "length", msF }, { "spiceTone", "tone", hzF } }));
    for (auto* l : lanes) addAndMakeVisible (l);

    for (auto& s : std::vector<KnobSpec> { { "release", "release", msF }, { "high", "high", dbF }, { "vol", "volume", dbF } })
    {
        masterKnobs.add (new ui::Knob (proc.apvts, s, kMaster));
        addAndMakeVisible (masterKnobs.getLast());
    }

    // preset browser
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedItemIndex()); };
    addAndMakeVisible (presetBox);

    auto step = [this] (int d)
    {
        const int n = proc.getNumPrograms();
        presetBox.setSelectedItemIndex ((presetBox.getSelectedItemIndex() + d + n) % n, juce::sendNotification);
    };
    prevBtn.onClick = [step] { step (-1); };
    nextBtn.onClick = [step] { step (+1); };
    addAndMakeVisible (prevBtn);  addAndMakeVisible (nextBtn);

    // view toggle
    specBtn.setRadioGroupId (1);  waveBtn.setRadioGroupId (1);
    specBtn.setClickingTogglesState (true);  waveBtn.setClickingTogglesState (true);
    specBtn.setToggleState (true, juce::dontSendNotification);
    specBtn.onClick = [this] { scope.setMode (ui::Scope3D::Mode::spectrum); };
    waveBtn.onClick = [this] { scope.setMode (ui::Scope3D::Mode::wave); };
    for (auto* b : { &specBtn, &waveBtn })
    {
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff9a3c));
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (b);
    }

    // root note (FL-style naming: 36 = C2)
    rootSlider.setSliderStyle (juce::Slider::LinearBar);
    rootSlider.setTextBoxIsEditable (false);
    rootSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff23262e));
    rootSlider.textFromValueFunction = [] (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 5); };
    addAndMakeVisible (rootSlider);
    rootAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "root", rootSlider);

    addAndMakeVisible (playBtn);
    startTimerHz (30);  // animates playheads + live param changes in the stem lanes
}

SubGunEditor::~SubGunEditor() { setLookAndFeel (nullptr); }

void SubGunEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff15171d), 0, 0, juce::Colour (0xff0a0b0e), 0, (float) getHeight(), false));
    g.fillAll();

    // extruded title (fake 3D)
    g.setFont (ui::F (30.0f, true));
    const juce::Rectangle<int> t (16, 8, 240, 42);
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (juce::Colour (0xff7a3d08).darker ((float) (6 - i) * 0.15f));
        g.drawText ("SUBGUN", t.translated (i, i), juce::Justification::left);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffc46b), 0, (float) t.getY(),
                                             juce::Colour (0xffe8741a), 0, (float) t.getBottom(), false));
    g.drawText ("SUBGUN", t, juce::Justification::left);

    // FX placeholder (next milestone)
    g.setColour (juce::Colour (0xff0e1015));
    g.fillRoundedRectangle (fxStrip.toFloat(), 5.0f);
    g.setColour (juce::Colour (0xffff4d5e));
    g.fillRoundedRectangle ((float) fxStrip.getX(), (float) fxStrip.getY(), 4.0f, (float) fxStrip.getHeight(), 2.0f);
    g.setFont (ui::F (17.0f, true));
    g.drawText ("fx", fxStrip.getX() + 14, fxStrip.getY() + 10, 80, 22, juce::Justification::left);
    g.setColour (juce::Colour (0xff5d6270));
    g.setFont (ui::F (13.0f));
    g.drawText ("distortion / filter / OTT / chorus  -  coming soon",
                fxStrip.withTrimmedLeft (552).withTrimmedRight (16), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xff8a8f9c));
    g.drawText ("master", fxStrip.getX() + 14, fxStrip.getY() + 32, 100, 18, juce::Justification::left);
}

void SubGunEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto top = r.removeFromTop (48);
    prevBtn.setBounds (top.getX() + 262, top.getY() + 10, 28, 28);
    presetBox.setBounds (top.getX() + 294, top.getY() + 10, 190, 28);
    nextBtn.setBounds (top.getX() + 488, top.getY() + 10, 28, 28);
    specBtn.setBounds (top.getX() + 540, top.getY() + 10, 80, 28);
    waveBtn.setBounds (top.getX() + 624, top.getY() + 10, 60, 28);
    rootSlider.setBounds (top.getX() + 710, top.getY() + 10, 70, 28);
    playBtn.setBounds (top.getRight() - 46, top.getY() + 1, 46, 46);

    r.removeFromTop (6);
    scope.setBounds (r.removeFromTop (220));
    r.removeFromTop (6);
    for (auto* l : lanes) { l->setBounds (r.removeFromTop (94)); r.removeFromTop (4); }

    fxStrip = r.removeFromTop (92);
    int x = fxStrip.getX() + 130;
    for (auto* k : masterKnobs) { k->setBounds (x, fxStrip.getY() + 3, 80, 86); x += 82; }
}
