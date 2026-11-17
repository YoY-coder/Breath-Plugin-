#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace breath::ui
{
    // Barre du haut, commune aux deux vues.
    class TopBar : public juce::Component
    {
    public:
        explicit TopBar (BreathAudioProcessor& p);

        std::function<void (int)> onViewChange;
        void setView (int v);
        void refresh();       // appelé à chaque changement du Hub
        void tick();          // ~30 Hz : valeurs globales

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        BreathAudioProcessor& proc;
        BreathHub& hub;

        juce::Label name;
        Segmented views { { "Instance", "Matrice" } };
        IconButton undoBtn { IconButton::Icon::undo }, redoBtn { IconButton::Icon::redo };
        SnapshotButtons snapshots;
        juce::Slider macro;
        juce::TextButton bypassAll { "Bypass all" };

        juce::Colour dot;
        int rankShown = 0;
        float macroShown = -1.0f;
    };
}
