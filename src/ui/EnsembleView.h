#pragma once

#include "SpectrumView.h"
#include "MatrixView.h"

namespace breath::ui
{
    // Onglet « Ensemble » : toutes les instances de la famille en même temps (façon Pro-Q 4).
    // Mosaïque de graphes en direct ; un clic ouvre la piste en grand, réglable comme la vue Instance.
    class EnsembleView : public juce::Component
    {
    public:
        explicit EnsembleView (BreathAudioProcessor& p);

        void refresh();
        void tick();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        void focus (int slot);
        void layoutTiles();

        BreathAudioProcessor& proc;
        BreathHub& hub;
        SpectrumView spectrum;
        RelationPanel panel;

        std::vector<int> order;
        std::vector<std::pair<juce::Rectangle<float>, int>> tiles;
        int focusSlot = -1, focusSource = -1, hoverTile = -1;
        juce::Rectangle<float> backArea, prevArea, nextArea, panelCard;

        std::array<std::array<float, kNumBands>, kMaxInstances> own {}, red {};
    };
}
