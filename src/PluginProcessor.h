#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Infos de piste envoyées par le DAW (nom + couleur), si l'hôte les fournit.
struct TrackInfo
{
    juce::String name;
    std::optional<juce::Colour> colour;
};

class BreathAudioProcessor final : public juce::AudioProcessor
{
public:
    BreathAudioProcessor();
    ~BreathAudioProcessor() override = default;

    // --- Audio ---------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // --- Interface -----------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // --- Infos ---------------------------------------------------------------
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

    // --- Sauvegarde dans le projet ------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Nom / couleur de piste (VST3 track info) ---------------------------
    void updateTrackProperties (const TrackProperties& properties) override;
    TrackInfo getTrackInfo() const;

    // Taille de l'interface mémorisée dans le projet
    int savedEditorWidth = 0;

    // Infos de diagnostic affichées dans l'interface
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int> currentBlockSize { 0 };
    std::atomic<bool> trackInfoReceived { false };

private:
    mutable juce::CriticalSection trackInfoLock;
    TrackInfo trackInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessor)
};
