#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace breath::ui
{
    // Palette sombre neutre, un seul accent (bleu système Apple).
    namespace colours
    {
        const juce::Colour bg      { 0xff161618 };
        const juce::Colour panel   { 0xff1f1f22 };
        const juce::Colour panel2  { 0xff26262a };
        const juce::Colour raised  { 0xff3a3a40 };
        const juce::Colour line    { 0xff2f2f34 };
        const juce::Colour line2   { 0xff3a3a40 };
        const juce::Colour text    { 0xfff2f2f4 };
        const juce::Colour text2   { 0xff9a9aa2 };
        const juce::Colour text3   { 0xff64646c };
        const juce::Colour accent  { 0xff0a84ff };
        const juce::Colour warning { 0xffff9f0a };
    }

    enum class Weight { regular, medium, semibold };

    juce::Font font (float height, Weight w = Weight::regular);

    // Largeur d'un texte dans une police.
    float textWidth (const juce::Font& f, const juce::String& text);

    // Couleur de la heatmap pour une réduction (dB positifs).
    juce::Colour heat (float reductionDb);

    // Dessine une carte (fond + bord arrondis).
    void drawCard (juce::Graphics& g, juce::Rectangle<float> r);

    // Petit titre en capitales (« DRY / WET », « HIÉRARCHIE »…).
    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r,
                      juce::Justification j = juce::Justification::centredLeft);

    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel();

        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override { return 7; }
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (12.0f, Weight::medium); }

        juce::Font getLabelFont (juce::Label&) override { return font (14.0f, Weight::semibold); }
        juce::Font getPopupMenuFont() override { return font (13.0f); }
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    };
}
