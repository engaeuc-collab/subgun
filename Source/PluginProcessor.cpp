#include "PluginProcessor.h"
#include "PluginEditor.h"

static const char* kStems[4] = { "sub", "trans", "body", "spice" };

juce::AudioProcessorValueTreeState::ParameterLayout SubGunProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const int v = 1;

    for (int i = 0; i < sp::Count; ++i)
    {
        const auto& d = sp::defs()[(size_t) i];
        if (i == sp::Shape)
            p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { d.id, v }, d.name,
                                                                 StringArray { "exp", "lin", "log" }, 0));
        else
            p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { d.id, v }, d.name,
                                                                NormalisableRange<float> (d.lo, d.hi, 0.01f, d.skew), d.def,
                                                                AudioParameterFloatAttributes().withLabel (d.unit)));
    }
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "high", v }, "High",
                 NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 3.6f, AudioParameterFloatAttributes().withLabel ("dB")));
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "vol", v }, "Volume",
                 NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
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

sp::ParamSet SubGunProcessor::readParams() const
{
    sp::ParamSet ps;
    for (int i = 0; i < sp::Count; ++i)
        ps.v[(size_t) i] = apvts.getRawParameterValue (sp::defs()[(size_t) i].id)->load();
    return ps;
}

void SubGunProcessor::applyParams (const sp::ParamSet& ps)
{
    for (int i = 0; i < sp::Count; ++i)
        if (auto* rp = apvts.getParameter (sp::defs()[(size_t) i].id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (ps.v[(size_t) i]));
}

void SubGunProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    auto P = [this] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
    const int root = (int) P ("root");

    // editor: play button (held) and variation audition (one-shot)
    const int gate = uiGate.load();
    if (gate != 0 && prevGate == 0)      { uiNote = root; midi.addEvent (juce::MidiMessage::noteOn (1, uiNote, 0.9f), 0); }
    else if (gate == 0 && prevGate != 0) { midi.addEvent (juce::MidiMessage::noteOff (1, uiNote), 0); }
    prevGate = gate;
    const int trig = uiTrigger.load();
    if (trig != prevTrigger) { prevTrigger = trig; midi.addEvent (juce::MidiMessage::noteOn (1, root, 0.9f), 0); }

    // settings, with mute / solo applied
    sg::Settings s = sp::toSettings (readParams());
    bool anySolo = false;
    for (auto* st : kStems) anySolo = anySolo || P (juce::String ("solo_") + st) > 0.5f;
    auto audible = [&] (int i)
    {
        const bool m  = P (juce::String ("mute_") + kStems[i]) > 0.5f;
        const bool so = P (juce::String ("solo_") + kStems[i]) > 0.5f;
        return ! m && (! anySolo || so);
    };
    if (! audible (0)) s.subGain = 0.0f;
    if (! audible (1)) s.transGain = 0.0f;
    if (! audible (2)) s.bodyGain = 0.0f;
    if (! audible (3)) s.spiceGain = 0.0f;
    const float pitchHz = P ("pitch");

    float* out = buffer.getWritePointer (0);
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        if (at > pos) engine.render (out + pos, at - pos, s);
        pos = at;
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), pitchHz * std::exp2 ((float) (msg.getNoteNumber() - root) / 12.0f), msg.getFloatVelocity());
        else if (msg.isNoteOff())  engine.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) engine.allOff();
    }
    if (pos < n) engine.render (out + pos, n - pos, s);

    // master: high shelf -> volume -> soft ceiling
    const float highDb = P ("high");
    if (highDb != lastHighDb)
    {
        lastHighDb = highDb;
        highShelf.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sampleRateHz, 4000.0, 0.7f, juce::Decibels::decibelsToGain (highDb));
    }
    const float vol = juce::Decibels::decibelsToGain (P ("vol"));
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
