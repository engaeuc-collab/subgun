#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* kStems[4] = { "sub", "trans", "body", "spice" };

juce::AudioProcessorValueTreeState::ParameterLayout SubGunProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    int v = 1;

    auto addF = [&] (const char* id, const char* name, float lo, float hi, float def, float skew, const char* unit)
    {
        NormalisableRange<float> r (lo, hi, 0.01f, skew);
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, v }, name, r, def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };

    // SUB
    addF ("sub",      "Sub",           -60.0f,   6.0f,   0.0f, 1.0f, "dB");
    addF ("diveAmt",  "Dive Amount",     0.0f,  36.0f,  10.0f, 1.0f, "st");
    addF ("diveTime", "Dive Time",       1.0f, 500.0f,  57.0f, 0.5f, "ms");
    addF ("swell",    "Swell",           0.0f, 300.0f,   0.0f, 0.5f, "ms");
    addF ("subDrive", "Sub Drive",       0.0f,  36.0f,   7.1f, 1.0f, "dB");
    addF ("hold",     "Hold",           10.0f, 4000.0f, 500.0f, 0.5f, "ms");
    // TRANSIENT
    addF ("trans",      "Transient",   -60.0f,   6.0f, -17.8f, 1.0f, "dB");
    addF ("transDecay", "Trans Decay",   5.0f, 400.0f,  70.0f, 0.5f, "ms");
    // BODY
    addF ("body",      "Body",         -60.0f,   6.0f,  -7.2f, 1.0f, "dB");
    addF ("bodyDecay", "Body Decay",     5.0f, 2000.0f, 120.0f, 0.4f, "ms");
    addF ("bodyDrive", "Body Drive",     0.0f,  36.0f,  17.3f, 1.0f, "dB");
    addF ("crunch",    "Crunch",         0.0f,   1.0f,   0.0f, 1.0f, "");
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "shape", v }, "Shape",
                                                         StringArray { "exp", "lin", "log" }, 0));
    // SPICE
    addF ("spice",     "Spice",        -60.0f,   6.0f, -20.0f, 1.0f, "dB");
    addF ("spicePos",  "Spice Pos",      0.0f, 600.0f,  40.0f, 0.6f, "ms");
    addF ("spiceLen",  "Spice Length",  10.0f, 600.0f, 130.0f, 0.6f, "ms");
    addF ("spiceTone", "Spice Tone",  200.0f, 8000.0f, 1800.0f, 0.4f, "Hz");
    // MASTER
    addF ("release",   "Release",        5.0f, 1000.0f,  80.0f, 0.5f, "ms");
    addF ("high",      "High",         -12.0f,  12.0f,   3.6f, 1.0f, "dB");
    addF ("vol",       "Volume",       -24.0f,   6.0f,   0.0f, 1.0f, "dB");
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { "root", v }, "Root Note", 12, 60, 36));

    for (auto* s : kStems)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { String ("mute_") + s, v }, String ("Mute ") + s, false));
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { String ("solo_") + s, v }, String ("Solo ") + s, false));
    }
    return { p.begin(), p.end() };
}

SubGunProcessor::SubGunProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool SubGunProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SubGunProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    engine.prepare (sampleRate);
    highShelf.reset();
    lastHighDb = 1000.0f;
}

sg::Settings SubGunProcessor::readSettings()
{
    auto P = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    auto lin = [] (float db) { return db <= -59.5f ? 0.0f : juce::Decibels::decibelsToGain (db); };

    bool anySolo = false;
    for (auto* s : kStems) anySolo = anySolo || P (juce::String ("solo_") + s) > 0.5f;
    auto audible = [&] (int i)
    {
        const bool m  = P (juce::String ("mute_") + kStems[i]) > 0.5f;
        const bool so = P (juce::String ("solo_") + kStems[i]) > 0.5f;
        return ! m && (! anySolo || so);
    };

    sg::Settings s;
    s.subGain   = audible (0) ? lin (P ("sub"))   : 0.0f;
    s.transGain = audible (1) ? lin (P ("trans")) : 0.0f;
    s.bodyGain  = audible (2) ? lin (P ("body"))  : 0.0f;
    s.spiceGain = audible (3) ? lin (P ("spice")) : 0.0f;
    s.diveSemis = P ("diveAmt");   s.diveMs = P ("diveTime");
    s.swellMs   = P ("swell");     s.subDriveDb = P ("subDrive");
    s.holdMs    = P ("hold");      s.releaseMs = P ("release");
    s.transDecayMs = P ("transDecay");
    s.bodyDecayMs  = P ("bodyDecay");  s.bodyDriveDb = P ("bodyDrive");  s.crunch = P ("crunch");
    s.spicePosMs = P ("spicePos");  s.spiceLenMs = P ("spiceLen");  s.spiceHz = P ("spiceTone");
    s.shape = (int) P ("shape");
    return s;
}

void SubGunProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    // editor play button -> note events
    const int gate = uiGate.load();
    if (gate != 0 && prevGate == 0)
    {
        uiNote = (int) apvts.getRawParameterValue ("root")->load();
        midi.addEvent (juce::MidiMessage::noteOn (1, uiNote, 0.9f), 0);
    }
    else if (gate == 0 && prevGate != 0)
        midi.addEvent (juce::MidiMessage::noteOff (1, uiNote), 0);
    prevGate = gate;

    const sg::Settings settings = readSettings();
    float* out = buffer.getWritePointer (0);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        if (at > pos) engine.render (out + pos, at - pos, settings);
        pos = at;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())                 engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())           engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allOff();
    }
    if (pos < n) engine.render (out + pos, n - pos, settings);

    // master: high shelf -> volume -> soft ceiling
    const float highDb = apvts.getRawParameterValue ("high")->load();
    if (highDb != lastHighDb)
    {
        lastHighDb = highDb;
        highShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRateHz, 4000.0, 0.7f, juce::Decibels::decibelsToGain (highDb));
    }
    const float vol = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("vol")->load());
    float peak = 0.0f;
    int w = vizPos.load();
    for (int i = 0; i < n; ++i)
    {
        float x = highShelf.processSample (out[i]) * vol;
        const float a = std::abs (x);
        if (a > 0.8f) x = (x < 0 ? -1.0f : 1.0f) * (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f));
        out[i] = x;
        peak = juce::jmax (peak, std::abs (x));
        vizBuf[(size_t) (w & vizMask)] = x;
        ++w;
    }
    vizPos.store (w & vizMask, std::memory_order_release);

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    const float db = peak > 1.0e-5f ? juce::Decibels::gainToDecibels (peak) : -100.0f;
    const float prev = peakDb.load();
    peakDb.store (db > prev ? db : prev - 0.6f);
    playheadMs.store (engine.newestTimeMs());
}

void SubGunProcessor::setCurrentProgram (int index)
{
    const auto& presets = getPresets();
    if (index < 0 || index >= (int) presets.size()) return;
    currentProgram = index;

    for (auto* param : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (rp->paramID != "root" && rp->paramID != "vol")
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& pv : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (pv.id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (pv.value));
}

void SubGunProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SubGunProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SubGunProcessor::createEditor() { return new SubGunEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SubGunProcessor(); }
