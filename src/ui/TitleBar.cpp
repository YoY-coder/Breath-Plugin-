#include "TitleBar.h"

namespace breath::ui
{
    namespace
    {
        // Ajoute un trait (polyligne ou arc) épaissi à `out`, extrémités franches.
        void strokeInto (juce::Path& out, const juce::Path& centreLine, float stroke)
        {
            juce::Path s;
            juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::butt).createStrokedPath (s, centreLine);
            out.addPath (s);
        }

        void line (juce::Path& out, float x1, float y1, float x2, float y2, float stroke)
        {
            juce::Path p;
            p.startNewSubPath (x1, y1);
            p.lineTo (x2, y2);
            strokeInto (out, p, stroke);
        }

        // « Bol » vers la droite : de (x0, yTop) à l'horizontale, demi-cercle, retour à (x0, yBot).
        void bowl (juce::Path& out, float x0, float yTop, float yBot, float stroke, float extraWidth)
        {
            const float r = (yBot - yTop) * 0.5f;
            const float xc = x0 + extraWidth;
            juce::Path p;
            p.startNewSubPath (x0, yTop);
            p.lineTo (xc, yTop);
            p.addCentredArc (xc, yTop + r, r, r, 0.0f, 0.0f, juce::MathConstants<float>::pi, false);
            p.lineTo (x0, yBot);
            strokeInto (out, p, stroke);
        }
    }

    TitleBar::TitleBar()
    {
        setInterceptsMouseClicks (false, false);
    }

    float TitleBar::addLetter (juce::Path& out, juce::juce_wchar c, float x, float h, float t)
    {
        const float half = t * 0.5f;
        const float gap = h * 0.11f;                 // la « coupure » caractéristique du logo
        const float pi = juce::MathConstants<float>::pi;

        switch (c)
        {
            case 'B':
            {
                line (out, x + half, 0.0f, x + half, h, t);
                const float x0 = x + t + gap;        // les bols sont détachés du fût
                bowl (out, x0, half, h * 0.5f, t, h * 0.10f);
                bowl (out, x0, h * 0.5f, h - half, t, h * 0.14f);
                return t + gap + h * 0.14f + (h * 0.5f - half) * 0.5f + half;
            }
            case 'R':
            {
                line (out, x + half, 0.0f, x + half, h, t);
                const float x0 = x + t + gap;        // bol détaché du fût, comme le D du logo
                bowl (out, x0, half, h * 0.56f, t, h * 0.14f);
                const float w = t + gap + h * 0.14f + (h * 0.56f - half) * 0.5f + half;
                line (out, x0 + h * 0.10f, h * 0.56f, x + w, h, t);
                return w;
            }
            case 'E':
            {
                const float w = h * 0.64f;
                line (out, x + half, 0.0f, x + half, h, t);
                line (out, x + t + gap * 0.7f, half, x + w, half, t);          // barres détachées du fût
                line (out, x + t + gap * 0.7f, h * 0.5f, x + w * 0.88f, h * 0.5f, t);
                line (out, x + t + gap * 0.7f, h - half, x + w, h - half, t);
                return w;
            }
            case 'A':
            {
                const float w = h * 0.9f;
                juce::Path p;
                p.startNewSubPath (x + half, h);
                p.lineTo (x + w * 0.5f, half * 0.6f);
                p.lineTo (x + w - half, h);
                strokeInto (out, p, t);
                line (out, x + w * 0.28f, h * 0.68f, x + w * 0.72f, h * 0.68f, t);
                return w;
            }
            case 'T':
            {
                const float w = h * 0.8f;
                line (out, x, half, x + w, half, t);
                line (out, x + w * 0.5f, t + gap * 0.7f, x + w * 0.5f, h, t);   // fût détaché de la barre
                return w;
            }
            case 'H':
            {
                const float w = h * 0.8f;
                line (out, x + half, 0.0f, x + half, h, t);
                line (out, x + w - half, 0.0f, x + w - half, h, t);
                line (out, x + t, h * 0.5f, x + w - t, h * 0.5f, t);
                return w;
            }
            case 'O':
            {
                // Cercle coupé en deux par de fines fentes en haut et en bas (comme le O de DROZ).
                const float r = (h - t) * 0.5f;
                const float cx = x + r + half, cy = h * 0.5f;
                const float a = std::asin (juce::jlimit (0.0f, 0.9f, gap * 0.5f / r));
                juce::Path left, right;
                left.addCentredArc (cx, cy, r, r, 0.0f, pi + a, 2.0f * pi - a, true);
                right.addCentredArc (cx, cy, r, r, 0.0f, a, pi - a, true);
                strokeInto (out, left, t);
                strokeInto (out, right, t);
                return 2.0f * r + t;
            }
            case 'N':
            {
                const float w = h * 0.82f;
                line (out, x + half, 0.0f, x + half, h, t);
                line (out, x + w - half, 0.0f, x + w - half, h, t);
                line (out, x + t * 0.8f, half, x + w - t * 0.8f, h - half, t * 1.05f);
                return w;
            }
            default:
                return h * 0.45f;   // espace
        }
    }

    void TitleBar::resized()
    {
        const float H = (float) getHeight();
        bold.clear();
        light.clear();
        if (H <= 0.0f)
            return;

        // « BREATH » : capitales épaisses, bien espacées.
        const float capH = H * 0.46f, tB = capH * 0.19f, trackB = capH * 0.30f;
        float x = 0.0f;
        for (auto c : juce::String ("BREATH"))
            x += addLetter (bold, c, x, capH, tB) + trackB;
        // « ONE » : plus fin, gris clair, très espacé (comme « Recordz »).
        const float tL = capH * 0.085f, trackL = capH * 0.55f;
        float xl = 0.0f;
        for (auto c : juce::String ("ONE"))
            xl += addLetter (light, c, xl, capH, tL) + trackL;

        boldBounds = bold.getBounds();
        lightBounds = light.getBounds();
        const float space = capH * 0.9f;
        const float total = boldBounds.getWidth() + space + lightBounds.getWidth();
        const float x0 = (float) getWidth() * 0.5f - total * 0.5f;
        const float y0 = (H - capH) * 0.5f;
        bold.applyTransform (juce::AffineTransform::translation (x0 - boldBounds.getX(), y0));
        light.applyTransform (juce::AffineTransform::translation (x0 + boldBounds.getWidth() + space - lightBounds.getX(), y0));
        boldBounds = bold.getBounds();
        lightBounds = light.getBounds();
    }

    void TitleBar::paint (juce::Graphics& g)
    {
        g.setColour (juce::Colour (0xfff4f4f6));
        g.fillPath (bold);
        g.setColour (juce::Colour (0xffc9c9cf));
        g.fillPath (light);

        // Petit point d'accent à la couleur de la famille, après « ONE ».
        const float d = boldBounds.getHeight() * 0.16f;
        g.setColour (accentTint);
        g.fillEllipse (lightBounds.getRight() + d * 1.4f, lightBounds.getBottom() - d, d, d);
    }
}
