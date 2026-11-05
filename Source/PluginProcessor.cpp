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

//==============================================================================
// Matcher implementation (kept in this file so the build can never miss it)
//==============================================================================
using namespace sp;


namespace
{
constexpr int    kCols = 240;
constexpr double kMaxSeconds = 2.5;
constexpr float  kPi = 3.14159265f;

float gauss (juce::Random& r)
{
    const float u1 = juce::jmax (1.0e-6f, r.nextFloat()), u2 = r.nextFloat();
    return std::sqrt (-2.0f * std::log (u1)) * std::cos (2.0f * kPi * u2);
}

std::vector<float> makePeaks (const float* x, int n)
{
    std::vector<float> p ((size_t) kCols * 2, 0.0f);
    for (int c = 0; c < kCols; ++c)
    {
        const int a = (int) ((juce::int64) n * c / kCols);
        const int b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / kCols));
        float mn = 0.0f, mx = 0.0f;
        for (int i = a; i < b && i < n; ++i) { mn = juce::jmin (mn, x[i]); mx = juce::jmax (mx, x[i]); }
        p[(size_t) c * 2] = mn;  p[(size_t) c * 2 + 1] = mx;
    }
    return p;
}

// ---------------------------------------------------------------- features
struct Features
{
    std::vector<float> a, b, env, wA, wB;
    int framesA = 0, framesB = 0;
};

class Evaluator
{
public:
    static constexpr int nbA = 40, nbB = 28;

    Evaluator (double sr, int numSamples)
        : n (numSamples), edgesA (makeEdges (2048, sr, nbA)), edgesB (makeEdges (512, sr, nbB)),
          bufA (4096, 0.0f), bufB (1024, 0.0f) {}

    void features (const float* x, Features& f)
    {
        f.framesA = juce::jmax (1, (n - 2048) / 1024 + 1);
        f.a.assign ((size_t) f.framesA * nbA, -100.0f);
        for (int fr = 0; fr < f.framesA; ++fr)
        {
            std::fill (bufA.begin(), bufA.end(), 0.0f);
            const int s = fr * 1024, len = juce::jmax (0, juce::jmin (2048, n - s));
            std::copy (x + s, x + s + len, bufA.begin());
            winA.multiplyWithWindowingTable (bufA.data(), 2048);
            fftA.performFrequencyOnlyForwardTransform (bufA.data());
            bands (bufA, edgesA, nbA, &f.a[(size_t) fr * nbA]);
        }
        f.framesB = juce::jmax (1, n / 512);
        f.b.assign ((size_t) f.framesB * nbB, -100.0f);
        for (int fr = 0; fr < f.framesB; ++fr)
        {
            std::fill (bufB.begin(), bufB.end(), 0.0f);
            const int s = fr * 512, len = juce::jmax (0, juce::jmin (512, n - s));
            std::copy (x + s, x + s + len, bufB.begin());
            winB.multiplyWithWindowingTable (bufB.data(), 512);
            fftB.performFrequencyOnlyForwardTransform (bufB.data());
            bands (bufB, edgesB, nbB, &f.b[(size_t) fr * nbB]);
        }
        const int ne = juce::jmax (1, n / 256);
        f.env.assign ((size_t) ne, -80.0f);
        for (int i = 0; i < ne; ++i)
        {
            double e = 0.0;
            const int s = i * 256, len = juce::jmin (256, n - s);
            for (int k = 0; k < len; ++k) e += (double) x[s + k] * x[s + k];
            f.env[(size_t) i] = juce::jmax (-80.0f, (float) (10.0 * std::log10 (e / 256.0 + 1.0e-10)));
        }
        weights (f.a, f.framesA, nbA, f.wA);
        weights (f.b, f.framesB, nbB, f.wB);
    }

    float loss (const Features& t, const Features& c) const
    {
        auto term = [] (const std::vector<float>& tv, const std::vector<float>& cv,
                        const std::vector<float>& w, int frames, int nb)
        {
            double s = 0.0, ws = 0.0;
            for (int fr = 0; fr < frames; ++fr)
            {
                double d = 0.0;
                for (int b = 0; b < nb; ++b)
                    d += std::abs (tv[(size_t) (fr * nb + b)] - cv[(size_t) (fr * nb + b)]);
                s += w[(size_t) fr] * d / nb;  ws += w[(size_t) fr];
            }
            return s / juce::jmax (1.0e-9, ws);
        };
        const double la = term (t.a, c.a, t.wA, t.framesA, nbA);
        const double lb = term (t.b, c.b, t.wB, t.framesB, nbB);
        double le = 0.0;
        for (size_t i = 0; i < t.env.size(); ++i) le += std::abs (t.env[i] - c.env[i]);
        le /= (double) t.env.size();
        return (float) (la + 0.6 * lb + 0.4 * le);
    }

private:
    static std::vector<int> makeEdges (int fftSize, double sr, int nb)
    {
        const double binHz = sr / fftSize, fLo = 30.0, fHi = juce::jmin (16000.0, 0.45 * sr);
        std::vector<int> e ((size_t) nb + 1);
        e[0] = juce::jmax (1, (int) std::lround (fLo / binHz));
        for (int i = 1; i <= nb; ++i)
        {
            const int v = (int) std::lround (fLo * std::pow (fHi / fLo, (double) i / nb) / binHz);
            e[(size_t) i] = juce::jmax (v, e[(size_t) i - 1] + 1);
        }
        for (auto& v : e) v = juce::jmin (v, fftSize / 2);
        return e;
    }

    static void bands (const std::vector<float>& mag, const std::vector<int>& edges, int nb, float* out)
    {
        for (int b = 0; b < nb; ++b)
        {
            const int lo = edges[(size_t) b];
            const int hi = juce::jmax (lo + 1, edges[(size_t) b + 1]);
            double p = 0.0;
            for (int k = lo; k < hi; ++k) p += (double) mag[(size_t) k] * mag[(size_t) k];
            p /= (hi - lo);
            out[b] = juce::jmax (-100.0f, (float) (10.0 * std::log10 (p + 1.0e-12)));
        }
    }

    static void weights (const std::vector<float>& v, int frames, int nb, std::vector<float>& w)
    {
        std::vector<float> fm ((size_t) frames, -200.0f);
        float gmax = -200.0f;
        for (int fr = 0; fr < frames; ++fr)
        {
            for (int b = 0; b < nb; ++b) fm[(size_t) fr] = juce::jmax (fm[(size_t) fr], v[(size_t) (fr * nb + b)]);
            gmax = juce::jmax (gmax, fm[(size_t) fr]);
        }
        w.resize ((size_t) frames);
        for (int fr = 0; fr < frames; ++fr)
            w[(size_t) fr] = 0.2f + 0.8f * juce::jlimit (0.0f, 1.0f, (fm[(size_t) fr] - (gmax - 50.0f)) / 50.0f);
    }

    int n;
    std::vector<int> edgesA, edgesB;
    std::vector<float> bufA, bufB;
    juce::dsp::FFT fftA { 11 }, fftB { 9 };
    juce::dsp::WindowingFunction<float> winA { 2048, juce::dsp::WindowingFunction<float>::hann },
                                        winB { 512,  juce::dsp::WindowingFunction<float>::hann };
};

float peakFreq (const float* x, int n, double sr, int start, int order, float fMin, float fMax)
{
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int len = juce::jmax (0, juce::jmin (N, n - start));
    for (int i = 0; i < len; ++i)
        buf[(size_t) i] = x[start + i] * (0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) (N - 1)));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    const int kLo = juce::jmax (2, (int) std::ceil (fMin * N / sr));
    const int kHi = juce::jmin (N / 2 - 2, (int) std::floor (fMax * N / sr));
    if (kHi <= kLo) return 0.0f;
    int k = kLo;
    for (int i = kLo; i <= kHi; ++i) if (buf[(size_t) i] > buf[(size_t) k]) k = i;
    const float a = std::log (buf[(size_t) k - 1] + 1.0e-9f), b = std::log (buf[(size_t) k] + 1.0e-9f),
                c = std::log (buf[(size_t) k + 1] + 1.0e-9f);
    const float d = a - 2.0f * b + c;
    const float off = std::abs (d) > 1.0e-9f ? 0.5f * (a - c) / d : 0.0f;
    return ((float) k + juce::jlimit (-0.5f, 0.5f, off)) * (float) sr / (float) N;
}

void addV (ParamSet& p, int i, float d) { p.v[(size_t) i] = clampV (i, p.v[(size_t) i] + d); }
void mulV (ParamSet& p, int i, float f) { p.v[(size_t) i] = clampV (i, p.v[(size_t) i] * f); }

void jitter (ParamSet& p, juce::Random& rng, float sigma)
{
    for (int i = 0; i < Count; ++i)
    {
        if (i == Shape) continue;
        if (i == Pitch) { p.v[(size_t) i] = clampV (i, p.v[(size_t) i] * (1.0f + gauss (rng) * sigma * 0.02f)); continue; }
        p.v[(size_t) i] = fromNorm (i, toNorm (i, p.v[(size_t) i]) + gauss (rng) * sigma);
    }
}
} // namespace

//==============================================================================
struct Matcher::Target
{
    double sr = 44100.0;
    std::vector<float> x;
    Features feats;
    std::vector<float> peaks;
    juce::String name;
    float seconds = 0.0f;
};

Matcher::Matcher() : juce::Thread ("SubGun matcher") { fmt.registerBasicFormats(); }
Matcher::~Matcher() { stopThread (6000); }

void Matcher::setStatus (Stage s, const juce::String& text, float prog)
{
    { const juce::ScopedLock sl (lock); status = text; }
    stage.store ((int) s);
    progress.store (prog);
}

juce::String Matcher::getStatus() const { const juce::ScopedLock sl (lock); return status; }

Snapshot Matcher::snapshot() const
{
    const juce::ScopedLock sl (lock);
    Snapshot s;
    s.batch = batch;
    if (target != nullptr) { s.targetPeaks = target->peaks; s.name = target->name; s.seconds = target->seconds; }
    return s;
}

void Matcher::startFit (const juce::File& sample, float spread)
{
    stopThread (6000);
    job = Job::fit;  file = sample;  jobSpread = spread;
    setStatus (loading, "Reading sample...", 0.0f);
    startThread();
}

void Matcher::startEvolve (const sp::ParamSet& parent, float spread)
{
    stopThread (6000);
    job = Job::evolve;  parentSet = parent;  jobSpread = spread;
    setStatus (varying, "Evolving variations...", 0.5f);
    startThread();
}

//==============================================================================
std::shared_ptr<Matcher::Target> Matcher::loadTarget (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 256) return nullptr;

    const int len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * kMaxSeconds * 1.3));
    const int nch = (int) r->numChannels;
    juce::AudioBuffer<float> buf (nch, len);
    r->read (&buf, 0, len, 0, true, true);

    std::vector<float> m ((size_t) len, 0.0f);
    float pk = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        float v = 0.0f;
        for (int c = 0; c < nch; ++c) v += buf.getSample (c, i);
        m[(size_t) i] = v / (float) nch;
        pk = juce::jmax (pk, std::abs (m[(size_t) i]));
    }
    if (pk < 1.0e-5f) return nullptr;

    int s = 0;
    while (s < len && std::abs (m[(size_t) s]) < 0.02f * pk) ++s;
    s = juce::jmax (0, s - 16);

    double sr = r->sampleRate;
    const int dec = juce::jmax (1, (int) std::floor (sr / 50000.0));
    std::vector<float> x;
    x.reserve ((size_t) (len - s) / (size_t) dec + 1);
    for (int i = s; i + dec <= len; i += dec)
    {
        float v = 0.0f;
        for (int k = 0; k < dec; ++k) v += m[(size_t) (i + k)];
        x.push_back (v / (float) dec);
    }
    sr /= dec;

    const size_t maxN = (size_t) (sr * kMaxSeconds);
    if (x.size() > maxN) x.resize (maxN);
    if (x.size() < 512) return nullptr;

    float p2 = 0.0f;
    for (auto v : x) p2 = juce::jmax (p2, std::abs (v));
    for (auto& v : x) v /= p2;

    auto t = std::make_shared<Target>();
    t->sr = sr;
    t->seconds = (float) ((double) x.size() / sr);
    t->name = f.getFileNameWithoutExtension();
    t->peaks = makePeaks (x.data(), (int) x.size());
    x.resize (juce::jmax ((size_t) 8192, x.size()), 0.0f);
    t->x = std::move (x);
    Evaluator ev (sr, (int) t->x.size());
    ev.features (t->x.data(), t->feats);
    return t;
}

//==============================================================================
// Quick measurements to seed the search near the right answer
ParamSet Matcher::analyse (const Target& T) const
{
    ParamSet p;
    const auto& x = T.x;
    const int n = (int) x.size();
    const double sr = T.sr;

    const int hop = juce::jmax (1, (int) (sr * 0.01));
    const int ne = n / hop;
    std::vector<float> rms ((size_t) ne, 0.0f);
    float maxR = 1.0e-9f;
    for (int i = 0; i < ne; ++i)
    {
        double e = 0.0;
        for (int k = 0; k < hop; ++k) e += (double) x[(size_t) (i * hop + k)] * x[(size_t) (i * hop + k)];
        rms[(size_t) i] = (float) std::sqrt (e / hop);
        maxR = juce::jmax (maxR, rms[(size_t) i]);
    }
    int lastActive = 0;
    for (int i = 0; i < ne; ++i) if (rms[(size_t) i] > 0.1f * maxR) lastActive = i;
    const float tEndMs = (float) (lastActive + 1) * 10.0f;

    // steady pitch from the middle of the active region
    int s0 = juce::jlimit (0, juce::jmax (0, n - 8192), (int) (0.5 * lastActive * hop));
    float fEnd = peakFreq (x.data(), n, sr, s0, 13, 25.0f, 250.0f);
    if (! (fEnd > 20.0f)) fEnd = 41.2f;

    // pitch dive
    const float f0 = peakFreq (x.data(), n, sr, 0, 11, fEnd * 0.9f, fEnd * 3.5f);
    const float fStart = juce::jmax (fEnd, f0);
    float diveMs = 120.0f;
    for (int fr = 0; fr < 40; ++fr)
    {
        const int s = fr * 256;
        if (s + 2048 > n) break;
        const float f = peakFreq (x.data(), n, sr, s, 11, fEnd * 0.85f, fEnd * 3.5f);
        if (f > 0.0f && f < fEnd * 1.04f) { diveMs = (float) (s + 1024) * 1000.0f / (float) sr; break; }
    }
    p.v[DiveAmt]  = clampV (DiveAmt, 12.0f * std::log2 (fStart / fEnd));
    p.v[DiveTime] = clampV (DiveTime, diveMs);
    p.v[Pitch]    = clampV (Pitch, fEnd);

    // clipping amount -> sub drive
    int clipped = 0, total = juce::jmax (1, (lastActive + 1) * hop);
    for (int i = 0; i < total && i < n; ++i) if (std::abs (x[(size_t) i]) > 0.95f) ++clipped;
    const float frac = (float) clipped / (float) total;
    p.v[SubDrive] = clampV (SubDrive, 3.0f + juce::jlimit (0.0f, 1.0f, frac / 0.3f) * 30.0f);

    p.v[Hold] = clampV (Hold, tEndMs - 40.0f);
    p.v[Release] = 40.0f;

    // transient: high-frequency energy early vs low-frequency energy
    const int n100 = juce::jmin (n, (int) (sr * 0.1));
    double hf = 0.0, lo = 0.0;
    float lp = 0.0f;
    const float a = 1.0f - std::exp (-2.0f * kPi * 150.0f / (float) sr);
    for (int i = 1; i < total && i < n; ++i)
    {
        lp += a * (x[(size_t) i] - lp);
        lo += (double) lp * lp;
        if (i < n100) { const float d = x[(size_t) i] - x[(size_t) i - 1]; hf += (double) d * d; }
    }
    const double hfRms = std::sqrt (hf / juce::jmax (1, n100));
    const double loRms = std::sqrt (lo / juce::jmax (1, total));
    p.v[Trans] = clampV (Trans, 20.0f * std::log10 ((float) (hfRms / (loRms + 1.0e-6)) + 1.0e-6f) - 3.0f);

    const int h5 = juce::jmax (1, (int) (sr * 0.005));
    float hmax = 1.0e-9f;
    std::vector<float> henv;
    for (int i = 1; (i + 1) * h5 < juce::jmin (n, (int) (sr * 0.6)); ++i)
    {
        double e = 0.0;
        for (int k = 0; k < h5; ++k) { const float d = x[(size_t) (i * h5 + k)] - x[(size_t) (i * h5 + k - 1)]; e += (double) d * d; }
        henv.push_back ((float) std::sqrt (e / h5));
        hmax = juce::jmax (hmax, henv.back());
    }
    int lastH = 0;
    for (size_t i = 0; i < henv.size(); ++i) if (henv[i] > 0.1f * hmax) lastH = (int) i;
    p.v[TransDecay] = clampV (TransDecay, (float) (lastH + 1) * 5.0f);

    p.v[Body] = -10.0f;
    p.v[Spice] = -24.0f;
    return p;
}

//==============================================================================
// (1+lambda) evolution strategy in normalised parameter space, scored by multi-resolution
// log-band spectra + level envelope against the dropped sample.
float Matcher::fit (const Target& T, ParamSet& best)
{
    const int n = (int) T.x.size();
    Evaluator ev (T.sr, n);
    Features cf;
    std::vector<float> buf ((size_t) n);

    auto score = [&] (const ParamSet& p)
    {
        sp::render (p, T.sr, buf.data(), n);
        float pk = 1.0e-6f;
        for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (buf[(size_t) i]));
        const float g = 1.0f / pk;
        for (auto& v : buf) v *= g;
        ev.features (buf.data(), cf);
        return ev.loss (T.feats, cf);
    };

    float bestLoss = score (best);
    std::array<float, Count> sc;  sc.fill (1.0f);
    sc[Pitch] = 0.12f;  sc[DiveAmt] = 0.5f;  sc[Release] = 0.5f;

    juce::Random rng (42);
    const juce::uint32 t0 = juce::Time::getMillisecondCounter();
    const int maxEvals = 460, lambda = 6;
    float sigma = 0.18f;
    int evals = 0, resets = 0;

    while (evals < maxEvals && ! threadShouldExit() && juce::Time::getMillisecondCounter() - t0 < 16000)
    {
        ParamSet genBest = best;
        float genLoss = bestLoss;
        for (int k = 0; k < lambda; ++k)
        {
            ParamSet c = best;
            const int forced = rng.nextInt (Count);
            for (int i = 0; i < Count; ++i)
            {
                if (i != forced && rng.nextFloat() > 0.3f) continue;
                if (i == Shape) { if (rng.nextFloat() < 0.25f) c.v[(size_t) i] = (float) rng.nextInt (3); continue; }
                c.v[(size_t) i] = fromNorm (i, toNorm (i, c.v[(size_t) i]) + gauss (rng) * sigma * sc[(size_t) i]);
            }
            const float l = score (c);
            ++evals;
            if (l < genLoss) { genLoss = l; genBest = c; }
        }
        if (genLoss < bestLoss) { best = genBest; bestLoss = genLoss; sigma = juce::jmin (0.35f, sigma * 1.2f); }
        else                    { sigma = juce::jmax (0.012f, sigma * 0.86f); }
        if (sigma < 0.02f && resets < 2) { sigma = 0.12f; ++resets; }
        progress.store (0.05f + 0.88f * (float) evals / (float) maxEvals);
    }
    return bestLoss;
}

//==============================================================================
Batch Matcher::makeBatch (const ParamSet& parent, float spread, int gen, const std::shared_ptr<Target>& t, bool firstGen)
{
    juce::Random rng ((juce::int64) juce::Time::getMillisecondCounter() * 7919 + gen);
    const float k = 0.4f + 1.6f * juce::jlimit (0.0f, 1.0f, spread);
    const float jit = 0.015f + 0.05f * spread;
    auto P = [&] (float f) { return std::pow (f, k); };

    Batch b;
    b.generation = gen;

    static const char* names[7] = { "Heavier", "Cleaner", "Long Dive", "Punchy", "Bright", "Gritty", "Mutant" };

    std::vector<Variation> out;
    { Variation v; v.name = firstGen ? "Recreation" : "Parent"; v.p = parent; out.push_back (v); }

    for (int f = 0; f < 7; ++f)
    {
        Variation v;  v.name = names[f];  v.p = parent;
        auto& q = v.p;
        switch (f)
        {
            case 0: addV (q, SubDrive, 6 * k); addV (q, BodyDrive, 5 * k); addV (q, Body, 3 * k);
                    addV (q, Crunch, 0.12f * k); mulV (q, Hold, 1.0f + 0.12f * k); break;
            case 1: addV (q, Crunch, -0.25f * k); addV (q, Trans, -4 * k); addV (q, Spice, -6 * k);
                    addV (q, BodyDrive, -5 * k); addV (q, SubDrive, -3 * k); break;
            case 2: addV (q, DiveAmt, 5 * k); mulV (q, DiveTime, P (1.5f)); break;
            case 3: mulV (q, DiveTime, P (0.65f)); mulV (q, TransDecay, P (0.7f)); addV (q, Trans, 4 * k);
                    mulV (q, Hold, P (0.8f)); mulV (q, BodyDecay, P (0.7f)); break;
            case 4: addV (q, Spice, 6 * k); mulV (q, SpiceTone, P (1.5f)); addV (q, Trans, 3 * k); addV (q, BodyDrive, 3 * k); break;
            case 5: addV (q, Crunch, 0.35f * k); addV (q, BodyDrive, 7 * k); mulV (q, SpicePos, P (0.7f));
                    q.v[Shape] = (float) ((juce::roundToInt (q.v[Shape]) + 1 + rng.nextInt (2)) % 3); break;
            default: jitter (q, rng, 0.16f * (0.5f + spread)); break;
        }
        jitter (q, rng, jit);
        out.push_back (v);
    }

    const int n = t != nullptr ? (int) t->x.size() : 44100;
    const double sr = t != nullptr ? t->sr : 44100.0;
    std::unique_ptr<Evaluator> ev;
    if (t != nullptr) ev = std::make_unique<Evaluator> (sr, n);
    Features cf;
    std::vector<float> buf ((size_t) n);

    for (auto& v : out)
    {
        sp::render (v.p, sr, buf.data(), n);
        float pk = 1.0e-6f;
        for (int i = 0; i < n; ++i) pk = juce::jmax (pk, std::abs (buf[(size_t) i]));
        for (auto& s : buf) s /= pk;
        v.peaks = makePeaks (buf.data(), t != nullptr ? (int) std::lround (t->seconds * sr) : n);
        if (ev != nullptr)
        {
            ev->features (buf.data(), cf);
            v.match = 100.0f * std::exp (-ev->loss (t->feats, cf) / 10.0f);
        }
    }
    b.items = std::move (out);
    return b;
}

//==============================================================================
void Matcher::run()
{
    if (job == Job::fit)
    {
        auto t = loadTarget (file);
        if (t == nullptr) { setStatus (idle, "Couldn't read that file - try a WAV or AIFF.", 0.0f); return; }
        {
            const juce::ScopedLock sl (lock);
            target = t;  batch = Batch();
        }
        generation = 1;
        version.fetch_add (1);

        setStatus (fitting, "Listening to the sample...", 0.02f);
        ParamSet best = analyse (*t);
        setStatus (fitting, "Recreating the sound...", 0.05f);
        fit (*t, best);
        if (threadShouldExit()) return;

        setStatus (varying, "Generating variations...", 0.96f);
        auto b = makeBatch (best, jobSpread, generation, t, true);
        { const juce::ScopedLock sl (lock); b.id = ++batchCounter; batch = b; }
        version.fetch_add (1);
        setStatus (idle, "Pick a variation to hear it. Choosing one evolves a new set.", 1.0f);
    }
    else
    {
        std::shared_ptr<Target> t;
        { const juce::ScopedLock sl (lock); t = target; }
        ++generation;
        auto b = makeBatch (parentSet, jobSpread, generation, t, false);
        if (threadShouldExit()) return;
        { const juce::ScopedLock sl (lock); b.id = ++batchCounter; batch = b; }
        version.fetch_add (1);
        setStatus (idle, "Pick a variation to hear it. Choosing one evolves a new set.", 1.0f);
    }
}
