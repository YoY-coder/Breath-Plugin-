#pragma once

#include "Widgets.h"
#include "SpectrumView.h"

namespace breath::ui
{
    // Avertissements discrets (isolement, export désynchronisé).
    void drawWarnings (juce::Graphics& g, juce::Rectangle<float> area, const BreathAudioProcessor& proc);

    // Vue « Instance » : visualisation + réglages de l'instance.
    class InstanceView : public juce::Component
    {
    public:
        explicit InstanceView (BreathAudioProcessor& p);

        void refresh() { spectrum.refresh(); }
        void tick();

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        BreathAudioProcessor& proc;
        SpectrumView spectrum;

        juce::Slider mix, lookahead;
        juce::AudioProcessorValueTreeState::SliderAttachment mixAtt, lookAtt;
        ParamSegmented stereo;
        ParamSwitch delta, bypass;

        std::array<juce::Rectangle<float>, 5> cards;
        int lastSync = -1;
    };
}
