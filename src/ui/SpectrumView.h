#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace breath::ui
{
    // Spectre de la piste + pistes sources + courbe de réduction,
    // avec réglage graphique de la relation sélectionnée (plage, seuil, courbe max façonnable).
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (BreathAudioProcessor& p);

        std::function<void()> onSelectionChange;
        void refresh();    // structure (hiérarchie, relations) modifiée
        void tick();       // ~60 fps

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        enum class Target { none, lowEdge, highEdge, maxLine, threshold, node, selectOther, chip, addChip };
        struct Hit { Target t = Target::none; int slot = -1; int node = -1; };

        juce::Rectangle<float> graphArea() const;
        float freqToX (float f) const;
        float xToFreq (float x) const;
        float specDbToY (float db) const;
        float yToSpecDb (float y) const;
        float redDbToY (float db) const;
        float yToRedDb (float y) const;

        Hit hitTest (juce::Point<float> p) const;
        float thresholdAt (const RelationSettings& rel, int source, float x) const;
        juce::Path curvePath (const float* values, std::function<float (float)> toY) const;
        void drawPill (juce::Graphics&, juce::Point<float> anchor, const juce::String&, juce::Colour, int align) const;
        void showAddMenu();
        void showSourceMenu (int source);
        void select (int source);

        BreathAudioProcessor& proc;
        BreathHub& hub;
        int me = -1;

        std::vector<int> sources;                 // instances qui creusent cette piste (relations actives)
        std::vector<std::pair<juce::Rectangle<float>, int>> chipAreas;
        juce::Rectangle<float> addChipArea;

        std::array<float, kNumBands> own {}, reduction {};
        std::array<std::array<float, kNumBands>, kMaxInstances> upper {};

        Hit drag, hover;
        RelationSettings dragStart;
        juce::Point<float> dragOrigin;
        int dragAxis = 0;
        float redScale = 15.0f;   // bords : 0 = indécis, 1 = fréquence, 2 = pente
    };
}
