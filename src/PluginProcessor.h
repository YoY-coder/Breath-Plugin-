#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Common.h"
#include "hub/BreathHub.h"
#include "dsp/Analyzer.h"
#include "dsp/FilterBank.h"

class BreathAudioProcessor final : public juce::AudioProcessor,
                                   public breath::HubClient,
                                   private juce::Timer
{
public:
    BreathAudioProcessor();
    ~BreathAudioProcessor() override;

    // --- Audio ---------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // --- Interface -----------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void updateTrackProperties (const TrackProperties& properties) override;

    // --- HubClient -----------------------------------------------------------
    int   getRank() const override;
    void  setRankFromHub (int rank) override;
    float getMacro() const override;
    void  setMacroFromHub (float v) override;
    bool  getBypassAll() const override;
    void  setBypassAllFromHub (bool b) override;
    int   getFilterBands() const override { return filterBandsWanted(); }
    int   filterBandsWanted() const noexcept;
    int   getFamily() const noexcept;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    bool  isDisplayBypassed() const noexcept { return displayBypassed; }
    void  prepareBandTables();
    std::array<float, 64> bandOct64 {};
    std::array<float, 128> bandOct128 {};
    std::array<float, 64> bandDetail64 {};
    std::array<float, 128> bandDetail128 {};

    // --- Accès pour l'interface ----------------------------------------------
    breath::BreathHub& getHub() noexcept          { return *hub; }
    int  getSlot() const noexcept                 { return slotIndex; }
    juce::AudioProcessorValueTreeState& getParams() noexcept { return apvts; }
    bool isIsolated() const;
    bool hasOfflineDesync() const;
    double getSampleRateSafe() const noexcept     { return currentSampleRate.load(); }
    int  getLatencyInSamples() const noexcept     { return currentLatency.load(); }

    int savedEditorWidth = 0;
    int currentView = 0;          // 0 = Instance, 1 = Matrice
    int instUpper = -1;           // vue Instance : source de la relation éditée (la cible est cette instance)
    int selectedUpper = -1;       // vue Matrice : source de la relation sélectionnée
    int selectedLower = -1;       // vue Matrice : piste creusée de la relation sélectionnée
    float getAutoMakeupDb() const noexcept { return autoCompShown.load (std::memory_order_relaxed); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override;

    // Moteur
    void startSegment (int64_t position);
    void resetDsp();
    void controlTick (int64_t s, int syncDelay, bool offline);
    bool fetchFrame (int u, int64_t r, bool offline, breath::FrameData& out);
    bool readLive (int u, breath::FrameData& out) const;
    void publishFrame (int64_t sampleIndex);

    juce::AudioProcessorValueTreeState apvts;
    juce::SharedResourcePointer<breath::BreathHub> hub;

    std::atomic<float>* pPriority = nullptr;
    std::atomic<float>* pBreath = nullptr;
    std::atomic<float>* pBypassAll = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pLookahead = nullptr;
    std::atomic<float>* pStereo = nullptr;
    std::atomic<float>* pMakeup = nullptr;
    std::atomic<float>* pAutoMakeup = nullptr;
    std::atomic<float>* pBands = nullptr;
    std::atomic<float>* pFamily = nullptr;
    std::atomic<float>* pAutoLook = nullptr;

    // Identité
    int slotIndex = -1;
    juce::Uuid instanceId;
    int creationSeq = 0;
    bool stateLoaded = false;
    int savedPeers = 0;
    juce::uint32 loadTime = 0;
    juce::uint32 createTime = 0;
    bool rankAssigned = false;

    // État audio
    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentLatency { 0 };
    std::atomic<float> autoCompShown { 0.0f };
    double sr = 44100.0;
    breath::Analyzer analyzer;
    breath::FilterBank bank;

    bool timelineMode = false;
    int64_t expectedPos = 0, segmentStart = INT64_MIN, liveCounter = 0, liveFrameCount = 0;

    // Valeurs « publiées » dans les trames (lues en début de bloc)
    float blockRank = 1.0f, blockMacro = 1.0f, blockBypassAll = 0.0f;

    // Relations (une par instance possible au-dessus)
    std::array<std::array<float, breath::kNumBands>, breath::kMaxInstances> env {};
    std::array<float, breath::kMaxInstances> weight {}, lastTarget {};
    std::array<bool, breath::kMaxInstances> weightInit {}, refInit {};
    std::array<std::array<float, breath::kNumBands>, breath::kMaxInstances> refLevel {};   // seuil auto
    bool displayBypassed = false;
    std::array<int, breath::kMaxInstances> stallCount {};
    bool stalledThisBlock = false;

    // Lookahead
    std::vector<float> delayBuf[2];
    int delayMask = 0, delayWrite = 0;

    // Lissages
    float mixS = 1.0f, bypS = 0.0f, deltaS = 0.0f, bypassAllS = 0.0f;
    float smoothCoef = 0.001f;
    float bypassAllTarget = 0.0f;
    float makeupS = 1.0f, makeupTarget = 1.0f, autoComp = 0.0f, blockMakeup = 0.0f;
    double avgBefore = 0.0, avgAfter = 0.0;
    bool blockAutoMakeup = false;
    std::array<float, breath::kNumBands> ownBands {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessor)
};
