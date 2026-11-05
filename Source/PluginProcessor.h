#pragma once
#include "Common.h"
#include "Synth.h"
#include "Presets.h"

class SubGunProcessor : public juce::AudioProcessor
{
public:
    SubGunProcessor();
    ~SubGunProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SubGun"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return (int) getPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override { return getPresets()[(size_t) index].name; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- shared with the editor (lock-free)
    static constexpr int vizSize = 1 << 15;
    static constexpr int vizMask = vizSize - 1;
    std::array<float, vizSize> vizBuf {};
    std::atomic<int>   vizPos { 0 };
    std::atomic<float> peakDb { -100.0f };
    std::atomic<float> playheadMs { -1.0f };
    std::atomic<int>   uiGate { 0 };           // editor play button

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    sg::Settings readSettings();

    sg::Engine engine;
    juce::dsp::IIR::Filter<float> highShelf;
    float lastHighDb = 1000.0f;
    int currentProgram = 0;
    int prevGate = 0, uiNote = 36;
    double sampleRateHz = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubGunProcessor)
};
