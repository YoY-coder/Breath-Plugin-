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

static void setParam (juce::RangedAudioParameter* p, float plainValue);

//==============================================================================
juce::String SlotParameter::getName (int maximumStringLength) const
{
    const auto prefix = owner != nullptr ? owner->getSlotName (slot) : (slot == 0 ? juce::String ("Self") : "Source " + juce::String (slot));
    return (prefix + juce::String::fromUTF8 (" – ") + AudioParameterFloat::getName (100)).substring (0, maximumStringLength);
}

juce::String BreathAudioProcessor::getSlotName (int k) const
{
    const juce::SpinLock::ScopedLockType sl (slotNameLock);
    return slotNames[(size_t) juce::jlimit (0, kNumSlots - 1, k)];
}

float BreathAudioProcessor::getSlotAmount (int k) const
{
    return (k >= 0 && k < kNumSlots) ? pSlotAmount[(size_t) k]->load() / 100.0f : -1.0f;
}

void BreathAudioProcessor::setSlotAmountFromHub (int k, float v)
{
    if (k >= 0 && k < kNumSlots)
        setParam (apvts.getParameter ("slot" + juce::String (k) + "_amount"), juce::jlimit (0.0f, 1.0f, v) * 100.0f);
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
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "makeup", 1 }, "Make-up",
                    NormalisableRange<float> (-12.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "autoMakeup", 1 }, "Make-up auto", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "family", 1 }, "Famille",
                    StringArray { "Alpha", String::fromUTF8 ("Bêta"), "Gamma", "Sigma", String::fromUTF8 ("Oméga") }, 0,
                    AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "autoLookahead", 1 }, "Lookahead auto", false,
                    AudioParameterBoolAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, String::fromUTF8 ("Résolution max"),
                    StringArray { "64 bandes", "128 bandes" }, 0, AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stereoType", 1 }, String::fromUTF8 ("Stéréo"),
                    StringArray { "L/R", "M/S" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "link", 1 }, "Link",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "focusA", 1 }, "Focus L/M",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "focusB", 1 }, "Focus R/S",
                    NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));

    // Emplacements d'automation des relations : Quantité et décalage du seuil (Self + 8 sources).
    for (int k = 0; k < kNumSlots; ++k)
    {
        layout.add (std::make_unique<SlotParameter> (ParameterID { "slot" + String (k) + "_amount", 1 }, String::fromUTF8 ("Quantité"),
                        NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, AudioParameterFloatAttributes().withLabel ("%"), k));
        layout.add (std::make_unique<SlotParameter> (ParameterID { "slot" + String (k) + "_thr", 1 }, "Seuil",
                        NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB"), k));
    }

    // Ancien sélecteur Stéréo / Mid / Side (V0.1–V0.2), gardé pour relire les anciens projets.
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stereoMode", 1 }, String::fromUTF8 ("Mode stéréo (ancien)"),
                    StringArray { String::fromUTF8 ("Stéréo"), "Mid", "Side" }, 0, AudioParameterChoiceAttributes().withAutomatable (false)));
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
    pStereoType = apvts.getRawParameterValue ("stereoType");
    pLink      = apvts.getRawParameterValue ("link");
    pFocusA    = apvts.getRawParameterValue ("focusA");
    pFocusB    = apvts.getRawParameterValue ("focusB");
    for (int k = 0; k < kNumSlots; ++k)
    {
        pSlotAmount[(size_t) k] = apvts.getRawParameterValue ("slot" + juce::String (k) + "_amount");
        pSlotThr[(size_t) k]    = apvts.getRawParameterValue ("slot" + juce::String (k) + "_thr");
        if (auto* sp = dynamic_cast<SlotParameter*> (apvts.getParameter ("slot" + juce::String (k) + "_amount"))) sp->owner = this;
        if (auto* sp = dynamic_cast<SlotParameter*> (apvts.getParameter ("slot" + juce::String (k) + "_thr")))    sp->owner = this;
    }
    pMakeup    = apvts.getRawParameterValue ("makeup");
    pBands     = apvts.getRawParameterValue ("bands");
    pFamily    = apvts.getRawParameterValue ("family");
    pAutoLook  = apvts.getRawParameterValue ("autoLookahead");
    pAutoMakeup = apvts.getRawParameterValue ("autoMakeup");

    instanceId = juce::Uuid();
    slotIndex = hub->registerInstance (this, instanceId, creationSeq);
    createTime = juce::Time::getMillisecondCounter();

    prepareBandTables();

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

void BreathAudioProcessor::prepareBandTables()
{
    // Détail low / high : fondu entre 250 Hz (bas) et 4 kHz (haut).
    auto det = [] (float f) { return juce::jlimit (0.0f, 1.0f, std::log2 (f / 250.0f) / 4.0f); };
    for (int j = 0; j < 64; ++j)  { bandOct64[(size_t) j]  = std::log2 (bandCentreN (j, 64));  bandDetail64[(size_t) j]  = det (bandCentreN (j, 64)); }
    for (int j = 0; j < 128; ++j) { bandOct128[(size_t) j] = std::log2 (bandCentreN (j, 128)); bandDetail128[(size_t) j] = det (bandCentreN (j, 128)); }
}

juce::AudioProcessorParameter* BreathAudioProcessor::getBypassParameter() const
{
    // Le bouton d'activation du DAW pilote directement notre Bypass.
    return apvts.getParameter ("bypass");
}

int BreathAudioProcessor::getFamily() const noexcept
{
    return pFamily != nullptr ? juce::jlimit (0, kNumFamilies - 1, juce::roundToInt (pFamily->load())) : 0;
}

int BreathAudioProcessor::filterBandsWanted() const noexcept
{
    return pBands != nullptr && pBands->load() > 0.5f ? 128 : 64;
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
    // Noms des emplacements d'automation (« Kick – Quantité »…), mis à jour quand les affectations changent.
    if (slotIndex >= 0)
    {
        bool changed = false;
        for (int k = 0; k < kNumSlots; ++k)
        {
            const auto nm = hub->slotSourceName (slotIndex, k);
            const juce::SpinLock::ScopedLockType sl (slotNameLock);
            if (slotNames[(size_t) k] != nm) { slotNames[(size_t) k] = nm; changed = true; }
        }
        if (changed)
            updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    }

    if (slotIndex >= 0 && hub->familyOf (slotIndex) != getFamily())
        hub->setFamily (slotIndex, getFamily());

    // Nouvelle instance (pas d'état chargé) : elle se place en bas de la hiérarchie.
    if (! rankAssigned && ! stateLoaded && slotIndex >= 0 && juce::Time::getMillisecondCounter() - createTime > 300)
    {
        rankAssigned = true;
        int maxRank = 0;
        for (int s : hub->hierarchy (getFamily()))
            if (s != slotIndex && hub->info (s).client != nullptr)
                maxRank = juce::jmax (maxRank, hub->info (s).client->getRank());
        setRankFromHub (juce::jmin (kMaxInstances, maxRank + 1));

        // Nouvelle instance : Self activé d'office (anti-résonances, seuil auto).
        auto self = hub->getRelation (slotIndex, slotIndex);
        self.assigned = true;
        self.enabled = true;
        self.autoThreshold = true;
        hub->setRelation (slotIndex, slotIndex, self);
        hub->sendChangeMessage();
    }
}

bool BreathAudioProcessor::isIsolated() const
{
    return slotIndex >= 0 && savedPeers > 0 && hub->numActive (getFamily()) == 1
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
    anaA.prepare (sampleRate);
    anaB.prepare (sampleRate);
    bank.prepare (sampleRate, filterBandsWanted());

    const int maxDelay = (int) std::ceil (0.05 * sampleRate) + 2;   // 50 ms (lookahead auto compris)
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
    anaA.reset();
    anaB.reset();
    chA.fill (kSilenceDb);
    chB.fill (kSilenceDb);
    bank.reset();
    for (auto& e : env) e.fill (0.0f);
    weight.fill (0.0f);
    lastTarget.fill (0.0f);
    weightInit.fill (false);
    refInit.fill (false);
    stallCount.fill (0);
    for (auto& d : delayBuf) std::fill (d.begin(), d.end(), 0.0f);
    delayWrite = 0;

    mixS = pMix->load() / 100.0f;
    bypS = pBypass->load() > 0.5f ? 1.0f : 0.0f;
    deltaS = pDelta->load() > 0.5f ? 1.0f : 0.0f;
    bypassAllS = 0.0f;
    bypassAllTarget = 0.0f;
    autoComp = 0.0f;
    avgBefore = avgAfter = 0.0;
    ownBands.fill (kSilenceDb);
    makeupS = makeupTarget = juce::Decibels::decibelsToGain (pMakeup->load());

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
    analyzer.compute (d.bands, (int) floorMod (frameIndex, Analyzer::kLowsEvery));
    d.rank = blockRank;
    d.macro = blockMacro;
    d.bypassAll = blockBypassAll;
    std::copy (d.bands, d.bands + kNumBands, ownBands.begin());

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

// Décalage du seuil (points de façonnage) moyenné sur les bandes fines d'une bande de filtrage.
static inline float thrOffsetGrid (const AtomicRelation& relA, int j, int stride) noexcept
{
    float v = 0.0f;
    for (int k = 0; k < stride; ++k)
        v += relA.thrOffset[(size_t) (j * stride + k)].load (std::memory_order_relaxed);
    return v / (float) stride;
}

void BreathAudioProcessor::controlTick (int64_t s, int syncDelay, bool offline)
{
    auto& me = hub->slot (slotIndex);
    const int myFamily = me.family.load (std::memory_order_relaxed);
    const int64_t r = s - syncDelay;

    // 1. Trames des instances de la même famille, au même instant de la timeline.
    std::array<FrameData, kMaxInstances> fr;
    std::array<bool, kMaxInstances> got {};

    for (int u = 0; u < kMaxInstances; ++u)
    {
        const auto& su = hub->slot (u);
        if (! su.active.load (std::memory_order_relaxed) || su.family.load (std::memory_order_relaxed) != myFamily)
            continue;
        if (u == slotIndex && ! timelineMode)
        {
            got[(size_t) u] = true;
            std::copy (ownBands.begin(), ownBands.end(), fr[(size_t) u].bands);
            fr[(size_t) u].rank = blockRank;
            fr[(size_t) u].macro = blockMacro;
            fr[(size_t) u].bypassAll = blockBypassAll;
            continue;
        }
        got[(size_t) u] = timelineMode ? fetchFrame (u, r, offline, fr[(size_t) u])
                                       : readLive (u, fr[(size_t) u]);
    }

    // 2. Valeurs globales de la famille (portées par son instance maîtresse).
    const int m = hub->getMasterSlot (myFamily);
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
    const int nb = bank.getNumBands();          // 64 ou 128
    const int stride = kNumBands / nb;
    const float* octs = (nb == 128) ? bandOct128.data() : bandOct64.data();
    const float* detT = (nb == 128) ? bandDetail128.data() : bandDetail64.data();
    std::array<float, kNumBands> total {};
    const float tickSec = (float) kTick / (float) sr;
    const float wCoef = 1.0f - std::exp (-tickSec / kWeightTimeSec);

    // Niveaux de la piste elle-même sur la grille : à l'instant r (masking, déterministe)
    // et « maintenant » (Self, sans délai).
    std::array<float, kNumBands> tgtGrid {}, ownGrid {};
    for (int j = 0; j < nb; ++j)
    {
        float a = kSilenceDb, b = kSilenceDb;
        for (int k = 0; k < stride; ++k)
        {
            if (got[(size_t) slotIndex]) a = juce::jmax (a, fr[(size_t) slotIndex].bands[j * stride + k]);
            b = juce::jmax (b, ownBands[(size_t) (j * stride + k)]);
        }
        tgtGrid[(size_t) j] = a;
        ownGrid[(size_t) j] = b;
    }

    auto smooth01 = [] (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); };

    for (int u = 0; u < kMaxInstances; ++u)
    {
        const bool isSelf = u == slotIndex;
        const auto& su = hub->slot (u);

        if (! su.active.load (std::memory_order_relaxed) || su.family.load (std::memory_order_relaxed) != myFamily)
        {
            env[(size_t) u].fill (0.0f);
            weight[(size_t) u] = 0.0f;
            weightInit[(size_t) u] = false;
            refInit[(size_t) u] = false;
            me.activityFrom[(size_t) u].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        const auto& relA = hub->relation (u, slotIndex);
        auto rel = relA.load();
        float target = lastTarget[(size_t) u];
        const bool srcValid = isSelf || got[(size_t) u];

        // Automation : Quantité et décalage du seuil viennent de l'emplacement de cette relation.
        float thrShift = 0.0f;
        if (rel.slot >= 0 && rel.slot < kNumSlots)
        {
            rel.amount = pSlotAmount[(size_t) rel.slot]->load() / 100.0f;
            thrShift = pSlotThr[(size_t) rel.slot]->load();
        }

        if (isSelf)
        {
            target = (rel.assigned && rel.enabled) ? 1.0f : 0.0f;
            if (! weightInit[(size_t) u]) { weight[(size_t) u] = target; weightInit[(size_t) u] = true; }
        }
        else if (got[(size_t) u])
        {
            const float ru = fr[(size_t) u].rank;
            const int seqU = su.creationSeq.load (std::memory_order_relaxed);
            const bool above = (std::abs (ru - myRank) > 0.5f) ? ru < myRank : seqU < mySeq;
            target = rel.isActive (above) ? 1.0f : 0.0f;
            if (! weightInit[(size_t) u]) { weight[(size_t) u] = target; weightInit[(size_t) u] = true; }
        }
        lastTarget[(size_t) u] = target;

        auto& w = weight[(size_t) u];
        w += (target - w) * wCoef;
        auto& e = env[(size_t) u];

        if (w < 1.0e-5f && target < 0.5f)
        {
            w = 0.0f;
            e.fill (0.0f);
            refInit[(size_t) u] = false;
            me.activityFrom[(size_t) u].store (0.0f, std::memory_order_relaxed);
            continue;
        }

        // Niveaux de la source sur la grille de filtrage (crête des bandes fines).
        std::array<float, kNumBands> src {};
        for (int j = 0; j < nb; ++j)
        {
            if (isSelf) { src[(size_t) j] = ownGrid[(size_t) j]; continue; }
            float lvl = kSilenceDb;
            if (srcValid)
                for (int k = 0; k < stride; ++k)
                    lvl = juce::jmax (lvl, fr[(size_t) u].bands[j * stride + k]);
            src[(size_t) j] = lvl;
        }

        // Seuil : fixe, ou automatique (spectre de la source lissé en fréquence et dans le temps).
        std::array<float, kNumBands> thr {};
        if (rel.autoThreshold)
        {
            std::array<float, kNumBands> sm = src;
            const float p = std::exp (-1.0f / (6.0f * (float) nb / 64.0f));    // ≈ 1 octave
            for (int j = 1; j < nb; ++j)      sm[(size_t) j] += p * (sm[(size_t) j - 1] - sm[(size_t) j]);
            for (int j = nb - 2; j >= 0; --j) sm[(size_t) j] += p * (sm[(size_t) j + 1] - sm[(size_t) j]);
            auto& ref = refLevel[(size_t) u];
            const float c = isSelf ? 1.0f - std::exp (-tickSec / 0.03f) : 1.0f - std::exp (-tickSec / 0.25f);
            for (int j = 0; j < nb; ++j)
            {
                if (! refInit[(size_t) u]) ref[(size_t) j] = sm[(size_t) j];
                else                       ref[(size_t) j] += (sm[(size_t) j] - ref[(size_t) j]) * c;
                thr[(size_t) j] = juce::jmax (-80.0f, ref[(size_t) j] + rel.autoOffset + thrShift + thrOffsetGrid (relA, j, stride));
            }
            refInit[(size_t) u] = true;
        }
        else
        {
            for (int j = 0; j < nb; ++j) thr[(size_t) j] = rel.threshold + thrShift + thrOffsetGrid (relA, j, stride);
        }

        // Réduction instantanée par bande (plage avec bords en fondu selon la pente).
        std::array<float, kNumBands> inst {}, mask {};
        double rangePower = 0.0, maskSum = 0.0;
        const float loOct = std::log2 (juce::jmin (rel.lowHz, rel.highHz)), hiOct = std::log2 (juce::jmax (rel.lowHz, rel.highHz));
        const float invLo = 1.0f / juce::jmax (0.02f, rel.lowSlope), invHi = 1.0f / juce::jmax (0.02f, rel.highSlope);
        const float amount = rel.amount * (rel.hard ? 4.0f : 1.0f);
        const bool masking = rel.masking && ! isSelf;
        std::array<float, kNumBands> curveMax {}, curveInt {};

        for (int j = 0; j < nb; ++j)
        {
            const float o = octs[j];
            const float mk = smooth01 ((o - loOct) * invLo + 0.5f) * smooth01 ((hiOct - o) * invHi + 0.5f);
            if (mk <= 1.0e-4f) continue;
            mask[(size_t) j] = mk;

            float inten = 0.0f, maxC = 0.0f;
            for (int k = 0; k < stride; ++k)
            {
                inten += relA.intensity[(size_t) (j * stride + k)].load (std::memory_order_relaxed);
                maxC  += relA.maxCurve[(size_t) (j * stride + k)].load (std::memory_order_relaxed);
            }
            inten /= (float) stride;
            maxC /= (float) stride;
            curveMax[(size_t) j] = maxC;
            curveInt[(size_t) j] = inten;

            const float lvl = srcValid ? src[(size_t) j] : kSilenceDb;
            float over = lvl - thr[(size_t) j];
            // Masking : on ne creuse que là où la piste creusée a elle aussi de l'énergie.
            if (masking) over = juce::jmin (over, tgtGrid[(size_t) j] - thr[(size_t) j]);
            over = RelationSettings::kneeCurve (over, rel.knee);   // seuil dur ou « soft »
            inst[(size_t) j] = juce::jlimit (0.0f, maxC, over * amount * inten) * mk;
            if (rel.resolution < 1.0f && lvl > -100.0f) rangePower += (double) (std::exp (lvl * 0.2302585093f) * mk);
            maskSum += mk;
        }

        // Résolution (de « large bande » à « spectral fin »), modulée par Détail low / high.
        const float res = juce::jlimit (0.0f, 1.0f, rel.resolution);
        const float sigmaBase = 0.5f * (std::pow (2.0f, (1.0f - res) * 6.0f) - 1.0f) * (float) nb / 64.0f;
        {
            std::array<float, kNumBands> pj {};
            bool any = false;
            for (int j = 0; j < nb; ++j)
            {
                const float d = rel.detailLow + (rel.detailHigh - rel.detailLow) * detT[j];
                const float sg = (sigmaBase + 0.5f) * std::exp2 (-2.0f * d) - 0.5f;
                pj[(size_t) j] = sg > 0.01f ? std::exp (-1.0f / sg) : 0.0f;
                any = any || pj[(size_t) j] > 0.0f;
            }
            if (any)
            {
                std::array<float, kNumBands> a = inst, n = mask;
                for (int j = 1; j < nb; ++j)       { const float p = pj[(size_t) j]; a[(size_t) j] += p * (a[(size_t) j - 1] - a[(size_t) j]); n[(size_t) j] += p * (n[(size_t) j - 1] - n[(size_t) j]); }
                for (int j = nb - 2; j >= 0; --j)  { const float p = pj[(size_t) j]; a[(size_t) j] += p * (a[(size_t) j + 1] - a[(size_t) j]); n[(size_t) j] += p * (n[(size_t) j + 1] - n[(size_t) j]); }
                for (int j = 0; j < nb; ++j)
                    inst[(size_t) j] = n[(size_t) j] > 1.0e-6f ? a[(size_t) j] / n[(size_t) j] * mask[(size_t) j] : 0.0f;
            }
        }

        if (res < 1.0f && maskSum > 0.0)
        {
            const float lvlRange = (float) (10.0 * std::log10 (rangePower * (64.0 / nb) + 1.0e-14));
            float meanThr = 0.0f;
            for (int j = 0; j < nb; ++j) meanThr += thr[(size_t) j] * mask[(size_t) j];
            meanThr /= (float) maskSum;
            const float over = RelationSettings::kneeCurve (lvlRange - meanThr, rel.knee) * amount;
            for (int j = 0; j < nb; ++j)
            {
                if (mask[(size_t) j] <= 0.0f) continue;
                float ov = over * curveInt[(size_t) j];
                if (masking) ov = juce::jmin (ov, RelationSettings::kneeCurve (tgtGrid[(size_t) j] - thr[(size_t) j], rel.knee) * amount * curveInt[(size_t) j]);
                const float bb = juce::jlimit (0.0f, curveMax[(size_t) j], ov);
                inst[(size_t) j] = res * inst[(size_t) j] + (1.0f - res) * bb * mask[(size_t) j];
            }
        }

        // Attaque / release (avec tilt bas / haut), puis poids de transition et macro Breath.
        auto& tc = timeCache[(size_t) u];
        auto differs = [] (float x, float y) { return ! juce::exactlyEqual (x, y); };
        if (differs (tc.attack, rel.attackMs) || differs (tc.release, rel.releaseMs) || tc.nb != nb
            || differs (tc.aLo, rel.attackTiltLow) || differs (tc.aHi, rel.attackTiltHigh)
            || differs (tc.rLo, rel.releaseTiltLow) || differs (tc.rHi, rel.releaseTiltHigh))
        {
            tc.attack = rel.attackMs; tc.release = rel.releaseMs; tc.nb = nb;
            tc.aLo = rel.attackTiltLow; tc.aHi = rel.attackTiltHigh; tc.rLo = rel.releaseTiltLow; tc.rHi = rel.releaseTiltHigh;
            const float laLo = std::log (juce::jlimit (0.1f, 10.0f, tc.aLo)), laHi = std::log (juce::jlimit (0.1f, 10.0f, tc.aHi));
            const float lrLo = std::log (juce::jlimit (0.1f, 10.0f, tc.rLo)), lrHi = std::log (juce::jlimit (0.1f, 10.0f, tc.rHi));
            for (int j = 0; j < nb; ++j)
            {
                const float t = detT[j];
                const float am = std::exp (laLo + (laHi - laLo) * t), rm = std::exp (lrLo + (lrHi - lrLo) * t);
                tc.cA[(size_t) j] = 1.0f - std::exp (-tickSec / juce::jmax (0.0001f, rel.attackMs * am * 0.001f));
                tc.cR[(size_t) j] = 1.0f - std::exp (-tickSec / juce::jmax (0.001f, rel.releaseMs * rm * 0.001f));
            }
        }
        float activity = 0.0f;

        for (int j = 0; j < nb; ++j)
        {
            auto& ev = e[(size_t) j];
            const float t = inst[(size_t) j];
            ev += (t - ev) * (t > ev ? tc.cA[(size_t) j] : tc.cR[(size_t) j]);
            const float c = ev * w * macro;
            total[(size_t) j] = juce::jmax (total[(size_t) j], c);
            activity = juce::jmax (activity, c);
        }

        me.activityFrom[(size_t) u].store (displayBypassed ? 0.0f : activity, std::memory_order_relaxed);
    }

    // Stéréo : Focus (part de chaque voie) et Link (100 % = même réduction sur les deux voies ;
    // en dessous, la voie la plus forte dans une bande reçoit plus de réduction que l'autre).
    std::array<float, kNumBands> totA {}, totB {};
    {
        const float link = juce::jlimit (0.0f, 1.0f, blockLink);
        for (int j = 0; j < nb; ++j)
        {
            float wA = 1.0f, wB = 1.0f;
            if (link < 0.999f)
            {
                float a = kSilenceDb, b = kSilenceDb;
                for (int k = 0; k < stride; ++k)
                {
                    a = juce::jmax (a, chA[(size_t) (j * stride + k)]);
                    b = juce::jmax (b, chB[(size_t) (j * stride + k)]);
                }
                const float peak = juce::jmax (a, b);
                wA = std::exp ((a - peak) * 0.1151293f);   // rapport d'amplitude (≤ 1)
                wB = std::exp ((b - peak) * 0.1151293f);
            }
            totA[(size_t) j] = total[(size_t) j] * blockFocusA * (link + (1.0f - link) * wA);
            totB[(size_t) j] = total[(size_t) j] * blockFocusB * (link + (1.0f - link) * wB);
        }
    }
    bank.setReductions (totA.data(), totB.data());

    // Make-up automatique : compense l'énergie retirée (mesurée sur le spectre de la piste).
    {
        double before = 0.0, after = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const double pw = std::exp ((double) ownBands[(size_t) b] * 0.2302585093);
            before += pw;
            after += pw * std::exp (-(double) total[(size_t) (b / stride)] * 0.2302585093);
        }
        // Moyenne glissante (~2 s) des puissances avant / après : on compense la perte de volume moyenne.
        const double c = 1.0 - std::exp (-(double) tickSec / 2.0);
        avgBefore += (before - avgBefore) * c;
        avgAfter  += (after - avgAfter) * c;
        autoComp = avgBefore > 1.0e-9 ? juce::jlimit (0.0f, 12.0f, (float) (10.0 * std::log10 (avgBefore / juce::jmax (avgAfter, 1.0e-30)))) : 0.0f;
        const float mkDb = blockMakeup + (blockAutoMakeup ? autoComp : 0.0f);
        makeupTarget = juce::Decibels::decibelsToGain (mkDb);
        autoCompShown.store (autoComp, std::memory_order_relaxed);
    }

    // Affichage : rien n'est retiré quand l'instance est en bypass.
    for (int b = 0; b < kNumBands; ++b)
        me.reductionCurve[(size_t) b].store (displayBypassed ? 0.0f : total[(size_t) (b / stride)], std::memory_order_relaxed);
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
    const int stType = juce::roundToInt (pStereoType->load());   // 0 = L/R, 1 = M/S
    blockLink = pLink->load() / 100.0f;
    blockFocusA = pFocusA->load() / 100.0f;
    blockFocusB = pFocusB->load() / 100.0f;

    // Changement de résolution (64 ↔ 128 bandes) : on reconstruit la banque de filtres.
    if (filterBandsWanted() != bank.getNumBands())
    {
        bank.prepare (sr, filterBandsWanted());
        for (auto& ev : env) ev.fill (0.0f);
    }
    blockMakeup = pMakeup->load();
    displayBypassed = pBypass->load() > 0.5f || bypassAllTarget > 0.5f;
    blockAutoMakeup = pAutoMakeup->load() > 0.5f;
    if (! blockAutoMakeup) makeupTarget = juce::Decibels::decibelsToGain (blockMakeup);

    // Lookahead (latence déclarée à l'hôte)
    const bool autoLook = pAutoLook->load() > 0.5f;
    const int lat = juce::jlimit (0, delayMask - 1, autoLook ? syncDelay
                                                             : juce::roundToInt (pLookahead->load() * 0.001f * (float) sr));
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
        if (right != nullptr)
        {
            anaA.push (stType == 1 ? 0.5f * (xL + xR) : xL);
            anaB.push (stType == 1 ? 0.5f * (xL - xR) : xR);
        }
        if (floorMod (s + 1, kHop) == 0)
        {
            publishFrame (s);
            if (right != nullptr && blockLink < 0.999f)
            {
                // Link < 100 % : on compare les deux voies pour doser la réduction de chacune.
                anaA.computeShort (chA.data());
                anaB.computeShort (chB.data());
            }
        }

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
            wL = wR = bank.processMono (dL);
        }
        else if (stType == 1)
        {
            // Mid / Side : chaque voie reçoit sa propre réduction (Focus, Link).
            float mid = 0.5f * (dL + dR), side = 0.5f * (dL - dR);
            bank.processStereo (mid, side);
            wL = mid + side;
            wR = mid - side;
        }
        else
        {
            wL = dL;
            wR = dR;
            bank.processStereo (wL, wR);
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
        makeupS += (makeupTarget - makeupS) * smoothCoef;
        const float byp = juce::jmax (bypS, bypassAllS);

        // Delta : ce qui est retiré (sans make-up). Sinon : signal traité + make-up.
        const float pL = wL * makeupS + ((dL - wL) - wL * makeupS) * deltaS;
        const float pR = wR * makeupS + ((dR - wR) - wR * makeupS) * deltaS;
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
    state.setProperty ("pluginVersion", JucePlugin_VersionString, nullptr);   // version de Breath qui a enregistré le projet
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

    // Attention : replaceState() complète l'arbre avec les paramètres manquants, d'où ces tests faits avant.
    const auto params = state.getChildWithName (apvts.state.getType()).createCopy();
    const bool hasStereoType = params.getChildWithProperty ("id", "stereoType").isValid();
    const bool hasSlots = params.getChildWithProperty ("id", "slot0_amount").isValid();
    if (params.isValid())
    {
        apvts.replaceState (params);

        // Projets V0.1–V0.2 : conversion de l'ancien sélecteur Stéréo / Mid / Side.
        if (! hasStereoType)
        {
            const int old = juce::roundToInt (pStereo->load());
            auto set = [this] (const char* id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
            set ("stereoType", old == 0 ? 0.0f : 1.0f);
            set ("link", 100.0f);
            set ("focusA", old == 2 ? 0.0f : 100.0f);
            set ("focusB", old == 1 ? 0.0f : 100.0f);
        }
    }

    stateLoaded = true;
    rankAssigned = true;
    if (slotIndex >= 0)
        hub->setFamily (slotIndex, getFamily());
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
        // Les emplacements d'automation reprennent les Quantités du projet (sauf si le projet les avait déjà).
        if (! hasSlots)
            hub->syncSlotAmounts (slotIndex);
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

    // Appelé par l'hôte sur le thread de l'interface. On ne remplace que ce que l'hôte fournit
    // (certains hôtes n'envoient que la couleur : il ne faut pas effacer le nom pour autant).
    std::optional<juce::String> name;
    if (properties.name.has_value() && properties.name->isNotEmpty())
        name = *properties.name;
    if (slotIndex >= 0)
        hub->setDawTrackInfo (slotIndex, name, colour);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreathAudioProcessor();
}
