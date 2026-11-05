#pragma once
#include "Ui.h"

class SubGunEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit SubGunEditor (SubGunProcessor&);
    ~SubGunEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { wave.setHover (true); }
    void fileDragExit (const juce::StringArray&) override { wave.setHover (false); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void showTab (int i);
    void loadSample (const juce::File& f);
    void refreshCards();
    void choose (int index);
    void evolveFrom (int index);

    SubGunProcessor& proc;
    th::GlossLNF lnf;   // declared first so it is destroyed last
    ui::Scope3D scope;

    // header
    juce::TextButton tabMatch { "Match" }, tabEngine { "Engine" }, tabFx { "FX" };
    ui::NoteBox note;
    ui::PlayButton play;
    ui::Meter meter;

    // pages
    juce::Component matchPage, enginePage, fxPage;

    // match page
    ui::WaveView wave;
    juce::TextButton specBtn { "spectrum" }, waveBtn { "wave" };
    juce::OwnedArray<ui::VariationCard> cards;
    juce::TextButton backBtn { "Back" }, evolveBtn { "Evolve" }, autoBtn { "Auto-evolve" };
    juce::Slider spreadSlider;
    juce::Label spreadLabel, infoLabel;

    // engine page
    juce::OwnedArray<ui::StemLane> lanes;
    juce::OwnedArray<ui::Knob> masterKnobs;

    // match state
    Batch current;
    std::vector<Batch> history;
    std::vector<float> targetPeaks;
    int selected = -1, lastVersion = -1, currentTab = 0;
    juce::Rectangle<int> controlsBar, masterStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubGunEditor)
};
