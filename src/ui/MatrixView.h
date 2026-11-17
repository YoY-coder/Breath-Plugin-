#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace breath::ui
{
    // Liste réordonnable (glisser-déposer) de la hiérarchie.
    class HierarchyList : public juce::Component
    {
    public:
        explicit HierarchyList (BreathAudioProcessor& p);
        std::function<void (int slot)> onSelect;

        void refresh();
        void tick();
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> rowArea (int index) const;
        juce::Rectangle<float> starArea (int index) const;

        BreathAudioProcessor& proc;
        BreathHub& hub;
        std::vector<int> order;
        std::array<float, kMaxInstances> meter {};
        int dragIndex = -1, dropIndex = -1;
        float dragY = 0.0f, grabOffset = 0.0f;
        bool dragged = false;
        int hoverStar = -1;
    };

    // Grille « qui creuse qui » avec heatmap.
    class RelationGrid : public juce::Component
    {
    public:
        explicit RelationGrid (BreathAudioProcessor& p);
        std::function<void()> onSelectionChange;

        void refresh();
        void tick();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        struct Cell { juce::Rectangle<float> r; int upper, lower; };
        void layoutCells();

        BreathAudioProcessor& proc;
        BreathHub& hub;
        std::vector<int> order;
        std::vector<Cell> cells;
        float cellSize = 46.0f;
        juce::Point<float> origin;
        std::array<std::array<float, kMaxInstances>, kMaxInstances> shown {};
    };

    // Réglages de la relation sélectionnée.
    class RelationPanel : public juce::Component
    {
    public:
        explicit RelationPanel (BreathAudioProcessor& p);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Row { juce::String label; juce::Slider* slider; std::function<juce::String()> value; };
        void apply();

        BreathAudioProcessor& proc;
        BreathHub& hub;
        juce::ToggleButton enabled;
        juce::Slider amount, maxRed, threshold, attack, release, range, resolution;
        std::vector<Row> rows;
        bool editing = false, updating = false;
    };

    // Vue « Matrice ».
    class MatrixView : public juce::Component
    {
    public:
        explicit MatrixView (BreathAudioProcessor& p);
        void refresh();
        void tick();
        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;

    private:
        BreathAudioProcessor& proc;
        HierarchyList list;
        RelationGrid grid;
        RelationPanel panel;
        juce::Rectangle<float> listCard, gridCard, panelCard;
    };
}
