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
        Segmented views { { "Instance", "Matrice", "Ensemble" } };
        IconButton undoBtn { IconButton::Icon::undo }, redoBtn { IconButton::Icon::redo };
        SnapshotButtons snapshots;
        juce::Slider macro;
        juce::TextButton bypassAll { "Bypass all" };

        // Lettre grecque de la famille (clic = changer de famille).
        struct FamilyButton : juce::Component, juce::SettableTooltipClient
        {
            int family = 0;
            std::function<void()> onClick;
            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                const auto c = BreathHub::familyColour (family);
                g.setColour (c.withAlpha (isMouseOver() ? 0.22f : 0.14f));
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (c);
                g.setFont (font (15.0f, Weight::semibold));
                g.drawText (BreathHub::familyGlyph (family), r.translated (0.0f, -1.0f), juce::Justification::centred);
            }
            void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
            void mouseEnter (const juce::MouseEvent&) override { repaint(); }
            void mouseExit (const juce::MouseEvent&) override  { repaint(); }
        } familyBtn;

        juce::Colour dot;
        float dotX = 50.0f;
        int rankShown = 0;
        float macroShown = -1.0f;
    };
}
