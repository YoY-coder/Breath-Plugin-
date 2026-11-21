#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace breath::ui
{
    // Contrôle segmenté façon macOS (« Instance | Matrice », « Stéréo | Mid | Side »).
    class Segmented : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit Segmented (juce::StringArray items);

        std::function<void (int)> onChange;
        void setSelected (int index, bool notify = false);
        int  getSelected() const noexcept { return selected; }
        float fontSize = 12.0f;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        juce::StringArray items;
        int selected = 0;
    };

    // Segmenté relié à un paramètre « choix » (automatisable).
    class ParamSegmented : public Segmented
    {
    public:
        ParamSegmented (juce::RangedAudioParameter& p, juce::StringArray items);

    private:
        juce::ParameterAttachment attachment;
    };

    // Petit bouton icône (undo / redo).
    class IconButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        enum class Icon { undo, redo };
        explicit IconButton (Icon i) : icon (i) {}

        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    private:
        Icon icon;
    };

    // Snapshots A / B / C.
    class SnapshotButtons : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<void (int)> onSelect;
        std::function<void()> onCopy;
        int active = 0;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    };

    // Réglage « tilt » façon Soothe : libellé + deux petites cases (bas | haut) réglées en glissant ↕.
    // Valeurs en « position » -1..1 (au centre = neutre) ; double-clic = neutre ; Shift = réglage fin.
    class TiltControl : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TiltControl (const juce::String& text, std::function<juce::String (float)> format) : label (text), fmt (std::move (format)) {}

        std::function<void()> onDragStart, onDragEnd;
        std::function<void (float lo, float hi)> onChange;
        void setValues (float lo, float hi) { v[0] = lo; v[1] = hi; repaint(); }
        float getLow() const noexcept  { return v[0]; }
        float getHigh() const noexcept { return v[1]; }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent& e) override { const int h = boxAt (e.position); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void enablementChanged() override { repaint(); }

    private:
        juce::Rectangle<float> box (int i) const;
        int boxAt (juce::Point<float> p) const;

        juce::String label;
        std::function<juce::String (float)> fmt;
        float v[2] { 0.0f, 0.0f };
        int active = -1, hover = -1;
        float startV = 0.0f, startY = 0.0f;
    };

    // Interrupteur avec libellé à gauche (non relié à un paramètre).
    class LabelledToggle : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit LabelledToggle (const juce::String& l) : label (l)
        {
            addAndMakeVisible (button);
            button.onClick = [this] { if (onChange) onChange (button.getToggleState()); };
        }
        std::function<void (bool)> onChange;
        void setState (bool b) { button.setToggleState (b, juce::dontSendNotification); }
        bool getState() const  { return button.getToggleState(); }
        void resized() override { button.setBounds (getLocalBounds().removeFromRight (34)); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (isEnabled() ? colours::text2 : colours::text3);
            g.setFont (font (12.0f));
            g.drawText (label, getLocalBounds().withTrimmedRight (38), juce::Justification::centredLeft, true);
        }
        void enablementChanged() override { repaint(); }

    private:
        juce::String label;
        juce::ToggleButton button;
    };

    // Interrupteur avec libellé à gauche, relié à un paramètre booléen.
    class ParamSwitch : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ParamSwitch (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& label);
        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        juce::ToggleButton button;
        juce::AudioProcessorValueTreeState::ButtonAttachment attachment;
        juce::String label;
    };
}
