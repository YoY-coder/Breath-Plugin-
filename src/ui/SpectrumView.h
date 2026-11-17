#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace breath::ui
{
    // Spectre de la piste + pistes du dessus + courbe de réduction,
    // avec réglage graphique de la relation sélectionnée.
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (BreathAudioProcessor& p);

        void refresh();    // structure (hiérarchie, relations) modifiée
        void tick();       // ~60 fps

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        enum class Target { none, lowEdge, highEdge, maxLine, threshold, selectOther, chip };
        struct Hit { Target t = Target::none; int slot = -1; };

        juce::Rectangle<float> graphArea() const;
        float freqToX (float f) const;
        float xToFreq (float x) const;
        float specDbToY (float db) const;
        float yToSpecDb (float y) const;
        float redDbToY (float db) const;
        float yToRedDb (float y) const;

        Hit hitTest (juce::Point<float> p) const;
        juce::Path curvePath (const float* values, std::function<float (float)> toY) const;
        void drawPill (juce::Graphics&, juce::Point<float> anchor, const juce::String&, juce::Colour, int align) const;

        BreathAudioProcessor& proc;
        BreathHub& hub;
        int me = -1;

        std::vector<int> uppers;                 // slots situés au-dessus (dans l'ordre)
        std::vector<std::pair<juce::Rectangle<float>, int>> chipAreas;

        std::array<float, kNumBands> own {}, reduction {};
        std::array<std::array<float, kNumBands>, kMaxInstances> upper {};

        Hit drag;
        RelationSettings dragStart;
        juce::Point<float> dragOrigin;
    };
}
