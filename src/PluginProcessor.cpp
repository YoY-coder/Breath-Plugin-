#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

using namespace breath;

namespace
{
    constexpr float kMaxLookaheadMs = 10.0f;
    constexpr float kWeightTimeSec  = 0.15f;   // transition lors d'un changement de hiérarchie
    constexpr float kSmoothTimeSec  = 0.02f;   // dry/wet, bypass, delta

    inline int64_t floorMod (int64_t a, int64_t b) noexcept { const auto m = a % b; return m < 0 ? m + b : m; }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BreathAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "priority", 1 }, String::fromUTF8 ("Priorité"), 1, kMaxInstances, 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "breath", 1 }, "Breath",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypassAll", 1 }, "Bypass All", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Delta", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Dry/Wet",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "lookahead", 1 }, "Lookahead",
                    NormalisableRange<float> (0.0f, kMaxLookaheadMs, 0.1f), 0.0f,
                    AudioParameterFloatAttributes().withLabel ("ms").withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stereoMode", 1 }, String::fromUTF8 ("Mode stéréo"),
                    StringArray { String::fromUTF8 ("Stéréo"), "Mid", "Side" }, 0));
    return layout;
}

BreathAudioProcessor::BreathAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Params", createLayout())
{
    pPriority  = apvts.getRawParameterValue ("priority");
    pBreath    = apvts.getRawParameterValue ("breath");
    pBypassAll = apvts.getRawParameterValue ("bypassAll");
    pBypass    = apvts.getRawParameterValue ("bypass");
    pDelta     = apvts.getRawParameterValue ("delta");
    pMix       = apvts.getRawParameterValue ("mix");
    pLookahead = apvts.getRawParameterValue ("lookahead");
    pStereo    = apvts.getRawParameterValue ("stereoMode");

    instanceId = juce::Uuid();
    slotIndex = hub->registerInstance (this, instanceId, creationSeq);
    createTime = juce::Time::getMillisecondCounter();

    // Préparation par défaut (au cas où l'hôte traiterait avant prepareToPlay).
    prepareToPlay (44100.0, 512);

    startTimer (250);
}

BreathAudioProcessor::~BreathAudioProcessor()
{
    stopTimer();
    if (slotIndex >= 0)
        hub->unregisterInstance (slotIndex);
}

//==============================================================================
int BreathAudioProcessor::getRank() const               { return juce::roundToInt (pPriority->load()); }
float BreathAudioProcessor::getMacro() const            { return pBreath->load() / 100.0f; }
bool BreathAudioProcessor::getBypassAll() const         { return pBypassAll->load() > 0.5f; }

static void setParam (juce::RangedAudioParameter* p, float plainValue)
{
    if (p == nullptr) return;
    const float norm = p->convertTo0to1 (plainValue);
    if (std::abs (p->getValue() - norm) < 1.0e-6f) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (norm);
    p->endChangeGesture();
}

void BreathAudioProcessor::setRankFromHub (int rank)        { setParam (apvts.getParameter ("priority"), (float) juce::jlimit (1, kMaxInstances, rank)); }
void BreathAudioProcessor::setMacroFromHub (float v)        { setParam (apvts.getParameter ("breath"), juce::jlimit (0.0f, 1.0f, v) * 100.0f); }
void BreathAudioProcessor::setBypassAllFromHub (bool b)     { setParam (apvts.getParameter ("bypassAll"), b ? 1.0f : 0.0f); }

void BreathAudioProcessor::timerCallback()
{
    // Nouvelle instance (pas d'état chargé) : elle se place en bas de la hiérarchie.
    if (! rankAssigned && ! stateLoaded && slotIndex >= 0 && juce::Time::getMillisecondCounter() - createTime > 300)
    {
        rankAssigned = true;
        int maxRank = 0;
        for (int s : hub->hierarchy())
            if (s != slotIndex && hub->info (s).client != nullptr)
                maxRank = juce::jmax (maxRank, hub->info (s).client->getRank());
        setRankFromHub (juce::jmin (kMaxInstances, maxRank + 1));
        hub->sendChangeMessage();
    }
}

bool BreathAudioProcessor::isIsolated() const
{
    return slotIndex >= 0 && savedPeers > 0 && hub->numActive() == 1
        && juce::Time::getMillisecondCounter() - loadTime > 4000;
}

bool BreathAudioProcessor::hasOfflineDesync() const
{
    return slotIndex >= 0 && hub->slot (slotIndex).offlineDesync.load();
}

//==============================================================================
void BreathAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    currentSampleRate = sampleRate;
    analyzer.prepare (sampleRate);
    bank.prepare (sampleRate);

    const int maxDelay = (int) std::ceil (kMaxLookaheadMs * 0.001 * sampleRate) + 2;
    int size = 1;
    while (size < maxDelay) size <<= 1;
    for (auto& d : delayBuf)
        d.assign ((size_t) size, 0.0f);
    delayMask = size - 1;

    smoothCoef = 1.0f - std::exp (-1.0f / (kSmoothTimeSec * (float) sampleRate));

    const int lat = juce::roundToInt (pLookahead->load() * 0.001f * (float) sampleRate);
    currentLatency = lat;
    setLatencySamples (lat);

    timelineMode = false;
    resetDsp();
    if (slotIndex >= 0)
    {
        hub->slot (slotIndex).segmentStart.store (INT64_MIN);
        hub->slot (slotIndex).clearFrames();
    }
}

bool BreathAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void BreathAudioProcessor::resetDsp()
{
    analyzer.reset();
    bank.reset();
    for (auto& e : env) e.fill (0.0f);
    weight.fill (0.0f);
    lastTarget.fill (0.0f);
    weightInit.fill (false);
    stallCount.fill (0);
    for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
    delayWrite = 0;

    mixS = pMix->load() / 100.0f;
    bypS = pBypass->load() > 0.5f ? 1.0f : 0.0f;
    deltaS = pDelta->load() > 0.5f ? 1.0f : 0.0f;
    bypassAllS = 0.0f;
    bypassAllTarget = 0.0f;

    if (slotIndex >= 0)
    {
        auto& me = hub->slot (slotIndex);
        for (auto& r : me.reductionCurve) r.store (0.0f, std::memory_order_relaxed);
        for (auto& a : me.activityFrom) a.store (0.0f, std::memory_order_relaxed);
    }
}

void BreathAudioProcessor::startSegment (int64_t position)
{
    segmentStart = position;
    resetDsp();
    if (slotIndex >= 0)
    {
        auto& me = hub->slot (slotIndex);
        me.segmentStart.store (INT64_MIN);
        me.clearFrames();
        me.segmentStart.store (position);
        me.offlineDesync.store (false);
    }
}

//==============================================================================
void BreathAudioProcessor::publishFrame (int64_t s)
{
    FrameData d;
    const int64_t frameIndex = floorDiv (s + 1, kHop) - 1;
    analyzer.compute (d.bands, floorMod (frameIndex, Analyzer::kLowsEvery) == 0);
    d.rank = blockRank;
    d.macro = blockMacro;
    d.bypassAll = blockBypassAll;

    auto& me = hub->slot (slotIndex);
    if (timelineMode)
    {
        const int64_t k = (s + 1) / kHop - 1;
        me.ring[(size_t) floorMod (k, kRingFrames)].write (k, segmentStart, d);
    }
    me.live.write (liveFrameCount++, INT64_MIN, d);
}

bool BreathAudioProcessor::readLive (int u, FrameData& out) const
{
    const auto& f = hub->slot (u).live;
    const auto s = f.seq.load (std::memory_order_acquire);
    return s >= 0 && f.read (s, INT64_MIN, out);
}

bool BreathAudioProcessor::fetchFrame (int u, int64_t r, bool offline, FrameData& out)
{
    const int64_t k = floorDiv (r + 1, kHop) - 1;
    if ((k + 1) * kHop <= segmentStart)
        return false;   // avant le début de lecture : silence (identique en temps réel et en export)

    auto& sl = hub->slot (u);
    auto& f = sl.ring[(size_t) floorMod (k, kRingFrames)];
    auto readCurrent = [&] (Frame& fr, int64_t idx)
    {
        // L'autre instance doit être dans le même segment de lecture que nous.
        return sl.segmentStart.load (std::memory_order_acquire) == segmentStart && fr.read (idx, segmentStart, out);
    };
    if (readCurrent (f, k))
        return true;

    auto fallback = [&]
    {
        for (int j = 1; j <= 8; ++j)
        {
            const int64_t kk = k - j;
            if ((kk + 1) * kHop <= segmentStart) break;
            if (readCurrent (sl.ring[(size_t) floorMod (kk, kRingFrames)], kk))
                return true;
        }
        return false;
    };

    if (! offline || u == slotIndex)
        return fallback();

    // Export (rendu hors temps réel) : on attend que l'autre instance ait publié cette trame.
    if (! stalledThisBlock)
    {
        const double start = juce::Time::getMillisecondCounterHiRes();
        const double budget = stallCount[(size_t) u] > 0 ? 2.0 : 200.0;
        auto lastProgress = sl.progress.load();
        double lastMove = start;
        int spins = 0;

        while (sl.active.load())
        {
            if (readCurrent (f, k))
            {
                stallCount[(size_t) u] = 0;
                return true;
            }

            const double now = juce::Time::getMillisecondCounterHiRes();
            const auto p = sl.progress.load();
            if (p != lastProgress) { lastProgress = p; lastMove = now; }

            if (now - lastMove > budget || now - start > 2000.0)
            {
                ++stallCount[(size_t) u];
                stalledThisBlock = true;
                break;
            }

            if (++spins < 200) std::this_thread::yield();
            else std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    }

    hub->slot (slotIndex).offlineDesync.store (true);
    return fallback();
}

void BreathAudioProcessor::controlTick (int64_t s, int syncDelay, bool offline)
{
    auto& me = hub->slot (slotIndex);
    const int64_t r = s - syncDelay;

    // 1. Trames de toutes les instances au même instant de la timeline.
    std::array<FrameData, kMaxInstances> fr;
    std::array<bool, kMaxInstances> got {};

    for (int u = 0; u < kMaxInstances; ++u)
    {
        if (! hub->slot (u).active.load (std::memory_order_relaxed))
            continue;
        if (u == slotIndex && ! timelineMode)
        {
            got[(size_t) u] = true;
            fr[(size_t) u].rank = blockRank;
            fr[(size_t) u].macro = blockMacro;
            fr[(size_t) u].bypassAll = blockBypassAll;
            continue;
        }
        got[(size_t) u] = timelineMode ? fetchFrame (u, r, offline, fr[(size_t) u])
                                       : readLive (u, fr[(size_t) u]);
    }

    // 2. Valeurs globales (portées par l'instance maîtresse).
    const int m = hub->getMasterSlot();
    float macro = 1.0f;
    bypassAllTarget = 0.0f;
    if (m >= 0 && got[(size_t) m])
    {
        macro = juce::jlimit (0.0f, 1.0f, fr[(size_t) m].macro);
        bypassAllTarget = fr[(size_t) m].bypassAll > 0.5f ? 1.0f : 0.0f;
    }

    const float myRank = got[(size_t) slotIndex] ? fr[(size_t) slotIndex].rank : blockRank;
    const int mySeq = me.creationSeq.load (std::memory_order_relaxed);

    // 3. Réduction voulue par chaque relation, combinée par MAXIMUM par bande.
    std::array<float, kNumBands> total {};
    const float tickSec = (float) kTick / (float) sr;
    const float wCoef = 1.0f - std::exp (-tickSec / kWeightTimeSec);

    for (int u = 0; u < kMaxInstances; ++u)
    {
        if (u == slotIndex)
            continue;

        if (! hub->slot (u).active.load (std::memory_order_relaxed))
        {
            env[(size_t) u].fill (0.0f);
            weight[(size_t) u] = 0.0f;
            weightInit[(size_t) u] = false;
            me.activityFrom[(size_t) u].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        const auto rel = hub->relation (u, slotIndex).load();
        float target = lastTarget[(size_t) u];

        if (got[(size_t) u])
        {
            const float ru = fr[(size_t) u].rank;
            const int seqU = hub->slot (u).creationSeq.load (std::memory_order_relaxed);
            const bool above = (std::abs (ru - myRank) > 0.5f) ? ru < myRank : seqU < mySeq;
            target = (above && rel.enabled) ? 1.0f : 0.0f;
            lastTarget[(size_t) u] = target;

            if (! weightInit[(size_t) u])
            {
                weight[(size_t) u] = target;
                weightInit[(size_t) u] = true;
            }
        }

        auto& w = weight[(size_t) u];
        w += (target - w) * wCoef;
        auto& e = env[(size_t) u];

        if (w < 1.0e-5f && target < 0.5f)
        {
            w = 0.0f;
            e.fill (0.0f);
            me.activityFrom[(size_t) u].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        // Réduction instantanée par bande (dans la plage de fréquences, bords adoucis).
        std::array<float, kNumBands> inst {}, mask {};
        const float bl = freqToBand (juce::jmin (rel.lowHz, rel.highHz));
        const float bh = freqToBand (juce::jmax (rel.lowHz, rel.highHz));
        double rangePower = 0.0, maskSum = 0.0;

        for (int b = 0; b < kNumBands; ++b)
        {
            const float fb = (float) b;
            mask[(size_t) b] = juce::jlimit (0.0f, 1.0f, juce::jmin (fb - bl + 1.0f, bh - fb + 1.0f));
            if (mask[(size_t) b] <= 0.0f)
                continue;
            const float lvl = got[(size_t) u] ? fr[(size_t) u].bands[b] : kSilenceDb;
            inst[(size_t) b] = juce::jlimit (0.0f, rel.maxReduction, (lvl - rel.threshold) * rel.amount) * mask[(size_t) b];
            if (rel.resolution < 1.0f) rangePower += std::exp ((double) lvl * 0.2302585093) * mask[(size_t) b];
            maskSum += mask[(size_t) b];
        }

        // Résolution : de « large bande » (une seule réduction sur toute la plage,
        // pilotée par l'énergie totale) à « spectral fin » (bande par bande).
        const float res = juce::jlimit (0.0f, 1.0f, rel.resolution);
        const float sigma = 0.5f * (std::pow (2.0f, (1.0f - res) * 6.0f) - 1.0f);
        if (sigma > 0.01f)
        {
            const float p = std::exp (-1.0f / sigma);
            std::array<float, kNumBands> a = inst, n = mask;
            for (int b = 1; b < kNumBands; ++b)       { a[(size_t) b] += p * (a[(size_t) b - 1] - a[(size_t) b]) ; n[(size_t) b] += p * (n[(size_t) b - 1] - n[(size_t) b]); }
            for (int b = kNumBands - 2; b >= 0; --b)  { a[(size_t) b] += p * (a[(size_t) b + 1] - a[(size_t) b]) ; n[(size_t) b] += p * (n[(size_t) b + 1] - n[(size_t) b]); }
            for (int b = 0; b < kNumBands; ++b)
                inst[(size_t) b] = n[(size_t) b] > 1.0e-6f ? a[(size_t) b] / n[(size_t) b] * mask[(size_t) b] : 0.0f;
        }

        if (res < 1.0f && maskSum > 0.0)
        {
            const float lvlRange = (float) (10.0 * std::log10 (rangePower + 1.0e-14));
            const float bb = juce::jlimit (0.0f, rel.maxReduction, (lvlRange - rel.threshold) * rel.amount);
            for (int b = 0; b < kNumBands; ++b)
                inst[(size_t) b] = res * inst[(size_t) b] + (1.0f - res) * bb * mask[(size_t) b];
        }

        // Attaque / release, puis poids de transition et macro Breath.
        const float cA = 1.0f - std::exp (-tickSec / juce::jmax (0.0001f, rel.attackMs * 0.001f));
        const float cR = 1.0f - std::exp (-tickSec / juce::jmax (0.001f, rel.releaseMs * 0.001f));
        float activity = 0.0f;

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& ev = e[(size_t) b];
            const float t = inst[(size_t) b];
            ev += (t - ev) * (t > ev ? cA : cR);
            const float c = ev * w * macro;
            total[(size_t) b] = juce::jmax (total[(size_t) b], c);
            activity = juce::jmax (activity, c);
        }

        me.activityFrom[(size_t) u].store (activity, std::memory_order_relaxed);
    }

    bank.setReductions (total.data());
    for (int b = 0; b < kNumBands; ++b)
        me.reductionCurve[(size_t) b].store (total[(size_t) b], std::memory_order_relaxed);
}

//==============================================================================
void BreathAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, getTotalNumInputChannels());

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);

    if (n == 0 || slotIndex < 0 || numCh == 0)
        return;

    auto& me = hub->slot (slotIndex);
    const bool offline = isNonRealtime();
    if (! offline)
        hub->reportRealtimeBlockSize (n);
    const int syncDelay = hub->getSyncDelay();

    // Valeurs de paramètres de ce bloc
    blockRank = pPriority->load();
    blockMacro = pBreath->load() / 100.0f;
    blockBypassAll = pBypassAll->load() > 0.5f ? 1.0f : 0.0f;
    const float mixT = pMix->load() / 100.0f;
    const float bypT = pBypass->load() > 0.5f ? 1.0f : 0.0f;
    const float deltaT = pDelta->load() > 0.5f ? 1.0f : 0.0f;
    const int mode = juce::roundToInt (pStereo->load());

    // Lookahead (latence déclarée à l'hôte)
    const int lat = juce::jlimit (0, delayMask - 1, juce::roundToInt (pLookahead->load() * 0.001f * (float) sr));
    if (lat != currentLatency.load())
    {
        currentLatency = lat;
        setLatencySamples (lat);
    }

    // Position dans la timeline
    bool playing = false;
    int64_t pos = 0;
    if (auto* ph = getPlayHead())
        if (auto info = ph->getPosition())
            if (info->getIsPlaying() && info->getTimeInSamples().hasValue())
            {
                playing = true;
                pos = *info->getTimeInSamples();
            }

    int64_t base;
    if (playing)
    {
        if (! timelineMode || pos != expectedPos)
        {
            timelineMode = true;
            startSegment (pos);
        }
        base = pos;
        expectedPos = pos + n;
    }
    else
    {
        if (timelineMode)
        {
            timelineMode = false;
            segmentStart = INT64_MIN;
            me.segmentStart.store (INT64_MIN);
            me.clearFrames();
        }
        base = liveCounter;
        liveCounter += n;
    }

    stalledThisBlock = false;
    auto* left = buffer.getWritePointer (0);
    auto* right = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const int64_t s = base + i;

        if (floorMod (s, kTick) == 0)
            controlTick (s, syncDelay, offline);

        const float xL = left[i];
        const float xR = right != nullptr ? right[i] : xL;

        analyzer.push (right != nullptr ? 0.5f * (xL + xR) : xL);
        if (floorMod (s + 1, kHop) == 0)
            publishFrame (s);

        // Lookahead : retard identique pour le signal traité et le signal dry.
        delayBuf[0][(size_t) delayWrite] = xL;
        delayBuf[1][(size_t) delayWrite] = xR;
        const int rd = (delayWrite - lat) & delayMask;
        const float dL = delayBuf[0][(size_t) rd];
        const float dR = delayBuf[1][(size_t) rd];
        delayWrite = (delayWrite + 1) & delayMask;

        float wL, wR;
        if (right == nullptr)
        {
            wL = wR = bank.process (0, dL);
        }
        else if (mode == 1 || mode == 2)
        {
            float mid = 0.5f * (dL + dR), side = 0.5f * (dL - dR);
            if (mode == 1) mid = bank.process (0, mid);
            else           side = bank.process (0, side);
            wL = mid + side;
            wR = mid - side;
        }
        else
        {
            wL = bank.process (0, dL);
            wR = bank.process (1, dR);
        }

        auto smooth = [this] (float& v, float target)
        {
            v += (target - v) * smoothCoef;
            if (std::abs (target - v) < 1.0e-4f) v = target;   // fin de fondu exacte (inaudible)
        };
        smooth (mixS, mixT);
        smooth (bypS, bypT);
        smooth (deltaS, deltaT);
        smooth (bypassAllS, bypassAllTarget);
        const float byp = juce::jmax (bypS, bypassAllS);

        const float pL = wL + ((dL - wL) - wL) * deltaS;
        const float pR = wR + ((dR - wR) - wR) * deltaS;
        float oL = dL + (pL - dL) * mixS;
        float oR = dR + (pR - dR) * mixS;
        if (byp >= 1.0f) { oL = dL; oR = dR; }   // bypass complet : signal d'origine exact
        else { oL += (dL - oL) * byp; oR += (dR - oR) * byp; }

        left[i] = oL;
        if (right != nullptr)
            right[i] = oR;
    }

    me.progress.fetch_add (n, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* BreathAudioProcessor::createEditor()
{
    return new BreathAudioProcessorEditor (*this);
}

void BreathAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("BreathState");
    state.setProperty ("version", 2, nullptr);
    state.setProperty ("id", instanceId.toString(), nullptr);
    state.setProperty ("seq", creationSeq, nullptr);
    state.setProperty ("editorWidth", savedEditorWidth, nullptr);
    state.setProperty ("view", currentView, nullptr);
    if (slotIndex >= 0)
        state.setProperty ("manualName", hub->info (slotIndex).manualName, nullptr);

    state.appendChild (apvts.copyState(), nullptr);
    if (slotIndex >= 0)
        state.appendChild (hub->exportModel(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BreathAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("BreathState"))
        return;

    savedEditorWidth = state.getProperty ("editorWidth", 0);
    currentView = state.getProperty ("view", 0);

    const auto params = state.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    stateLoaded = true;
    rankAssigned = true;
    loadTime = juce::Time::getMillisecondCounter();

    if (slotIndex < 0)
        return;

    if (state.hasProperty ("id"))
    {
        juce::Uuid id (state["id"].toString());
        int seq = state.getProperty ("seq", 0);
        hub->restoreIdentity (slotIndex, id, seq, state.getProperty ("manualName", "").toString());
        instanceId = id;
        creationSeq = seq;
    }

    const auto model = state.getChildWithName ("Matrix");
    if (model.isValid())
    {
        hub->mergeModel (model);
        savedPeers = 0;
        for (const auto& p : model.getChildWithName ("Peers"))
            if (p["id"].toString() != instanceId.toString())
                ++savedPeers;
    }
}

void BreathAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    std::optional<juce::Colour> colour;
    if (properties.colourARGB.has_value())
        colour = juce::Colour (*properties.colourARGB);

    // Appelé par l'hôte sur le thread de l'interface.
    if (slotIndex >= 0)
        hub->setDawTrackInfo (slotIndex, properties.name.value_or (juce::String()), colour);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreathAudioProcessor();
}
