#pragma once

#include "Theme.h"

namespace breath::ui
{
    // Bandeau de titre « BREATH ONE », dessiné dans le style du logo DROZ Recordz :
    // capitales géométriques épaisses, parties rondes séparées du fût par une fine coupure,
    // O coupé en deux ; « ONE » plus fin, gris clair et espacé. Vectoriel : net à toutes les tailles.
    class TitleBar : public juce::Component
    {
    public:
        TitleBar();
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick() {}

        juce::Colour accentTint = juce::Colour (0xff0a84ff);   // teinte de la famille (petit point d'accent)

    private:
        // Ajoute une lettre (contours « traits ») à `out` ; renvoie la largeur occupée.
        static float addLetter (juce::Path& out, juce::juce_wchar c, float x, float h, float stroke);
        juce::Path bold, light;
        juce::Rectangle<float> boldBounds, lightBounds;
    };
}
