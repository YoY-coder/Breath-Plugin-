#pragma once

#include "PluginProcessor.h"

class BreathAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit BreathAudioProcessorEditor (BreathAudioProcessor&);
    ~BreathAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int defaultWidth  = 900;
    static constexpr int defaultHeight = 560;

private:
    void timerCallback() override;

    BreathAudioProcessor& processor;
    TrackInfo shownTrackInfo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessorEditor)
};
