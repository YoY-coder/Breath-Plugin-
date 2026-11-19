#pragma once

#include "Widgets.h"
#include "SpectrumView.h"
#include "MatrixView.h"

namespace breath::ui
{
    // Avertissements discrets (isolement, export désynchronisé).
    void drawWarnings (juce::Graphics& g, juce::Rectangle<float> area, const BreathAudioProcessor& proc);

    // Vue « Instance » : visualisation + tous les réglages (relation et instance) au même endroit.
    class InstanceView : public juce::Component
    {
    public:
        explicit InstanceView (BreathAudioProcessor& p);

        void refresh() { spectrum.refresh(); panel.refresh(); }
        void tick();

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        BreathAudioProcessor& proc;
        SpectrumView spectrum;
        RelationPanel panel;

        juce::Slider mix, makeup, lookahead;
        juce::AudioProcessorValueTreeState::SliderAttachment mixAtt, makeupAtt, lookAtt;
        ParamSwitch autoMakeup;
        ParamSegmented stereo;
        ParamSwitch delta, bypass;

        std::array<juce::Rectangle<float>, 5> cards;
        juce::Rectangle<float> panelCard;
        int lastSync = -1;
        float lastAuto = -1.0f;
    };
}
