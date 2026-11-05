#pragma once
#include "Common.h"
#include "Synth.h"
#include "Params.h"
#include "Matcher.h"

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

    // No factory presets: sounds come from dropped samples. Hosts still expect one program.
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // sound <-> parameters (message thread)
    sp::ParamSet readParams() const;
    void applyParams (const sp::ParamSet& ps);
    void trigger() { uiTrigger.fetch_add (1); }

    juce::AudioProcessorValueTreeState apvts;
    Matcher matcher;

    // shared with the editor (lock-free)
    static constexpr int vizSize = 1 << 15;
    static constexpr int vizMask = vizSize - 1;
    std::array<float, vizSize> vizBuf {};
    std::atomic<int>   vizPos { 0 };
    std::atomic<float> peakDb { -100.0f };
    std::atomic<float> playheadMs { -1.0f };
    std::atomic<int>   uiGate { 0 };
    std::atomic<int>   uiTrigger { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    sg::Engine engine;
    juce::dsp::IIR::Filter<float> highShelf;
    float lastHighDb = 1000.0f;
    int prevGate = 0, prevTrigger = 0, uiNote = 36;
    double sampleRateHz = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubGunProcessor)
};
