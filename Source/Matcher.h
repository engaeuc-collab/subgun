#pragma once
#include "Params.h"

struct Variation
{
    juce::String name;
    sp::ParamSet p;
    std::vector<float> peaks;   // min/max pairs for the thumbnail
    float match = 0.0f;         // 0..100, how close it is to the dropped sample
};

struct Batch
{
    std::vector<Variation> items;
    int generation = 0;
    int id = 0;
};

struct Snapshot
{
    Batch batch;
    std::vector<float> targetPeaks;
    juce::String name;
    float seconds = 0.0f;
};

// Background worker. Fit: load sample -> analyse -> evolutionary search so the synth
// reproduces it -> build a batch of variations. Evolve: new batch around a chosen variation.
class Matcher : public juce::Thread
{
public:
    enum Stage { idle = 0, loading, fitting, varying };

    Matcher();
    ~Matcher() override;

    void startFit (const juce::File& sample, float spread);
    void startEvolve (const sp::ParamSet& parent, float spread);

    bool isBusy() const { return stage.load() != idle; }
    Snapshot snapshot() const;
    juce::String getStatus() const;

    std::atomic<int>   stage { 0 };
    std::atomic<int>   version { 0 };     // bumps whenever new results are published
    std::atomic<float> progress { 0.0f };

    void run() override;

private:
    struct Target;
    enum class Job { fit, evolve };

    std::shared_ptr<Target> loadTarget (const juce::File& f);
    sp::ParamSet analyse (const Target& t) const;
    float fit (const Target& t, sp::ParamSet& best);
    Batch makeBatch (const sp::ParamSet& parent, float spread, int generation, const std::shared_ptr<Target>& t, bool firstGen);
    void setStatus (Stage s, const juce::String& text, float prog);

    juce::AudioFormatManager fmt;
    mutable juce::CriticalSection lock;
    std::shared_ptr<Target> target;
    Batch batch;
    juce::String status;
    int batchCounter = 0, generation = 0;

    Job job = Job::fit;
    juce::File file;
    sp::ParamSet parentSet;
    float jobSpread = 0.35f;
};
