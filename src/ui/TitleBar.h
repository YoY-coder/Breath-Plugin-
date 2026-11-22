#pragma once

#include "Theme.h"

namespace breath::ui
{
    // Bandeau de titre : « BREATH ONE » en lettres manuscrites faites de points (pointillé irrégulier),
    // dégradé chaud et léger scintillement qui « respire ». Tout est vectoriel : net à toutes les tailles.
    class TitleBar : public juce::Component
    {
    public:
        TitleBar();
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();   // ~30 fps : animation douce

        juce::Colour accentTint = juce::Colour (0xff0a84ff);   // teinte de la famille

    private:
        struct Dot { juce::Point<float> p; float r, phase, t; };
        void buildDots();

        std::vector<Dot> dots;
        juce::Rectangle<float> textBounds;
        float time = 0.0f;
    };
}
