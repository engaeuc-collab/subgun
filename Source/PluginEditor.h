#pragma once
#include "Ui.h"

class SubGunEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SubGunEditor (SubGunProcessor&);
    ~SubGunEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { for (auto* l : lanes) l->repaint(); }


private:
    SubGunProcessor& proc;

    ui::KnobLNF lnf;                       // declared first -> destroyed last
    ui::Scope3D scope;
    juce::OwnedArray<ui::StemLane> lanes;
    juce::OwnedArray<ui::Knob> masterKnobs;

    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, specBtn { "spectrum" }, waveBtn { "wave" };
    juce::ComboBox presetBox;
    juce::Slider rootSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rootAtt;
    ui::PlayButton playBtn;

    juce::Rectangle<int> fxStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubGunEditor)
};
