#include "TitleBar.h"

namespace breath::ui
{
    TitleBar::TitleBar()
    {
        setInterceptsMouseClicks (false, false);
    }

    void TitleBar::resized()
    {
        buildDots();
    }

    void TitleBar::buildDots()
    {
        dots.clear();
        const float h = (float) getHeight();
        if (h <= 0.0f)
            return;

        // 1. Contours des lettres (majuscules, légère inclinaison et petites irrégularités par lettre).
        const juce::String text = "BREATH ONE";
        auto f = font (h * 0.74f, Weight::semibold);
        f.setExtraKerningFactor (0.16f);                 // lettres bien séparées
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, text, 0.0f, 0.0f);

        juce::Random rng (20251121);   // graine fixe : même dessin à chaque ouverture
        juce::Path letters;
        for (int i = 0; i < ga.getNumGlyphs(); ++i)
        {
            juce::Path gp;
            ga.getGlyph (i).createPath (gp);
            if (gp.isEmpty()) continue;
            const auto gb = gp.getBounds();
            const float rot = (rng.nextFloat() - 0.5f) * 0.08f;
            const float dy = (rng.nextFloat() - 0.5f) * h * 0.04f;
            gp.applyTransform (juce::AffineTransform::rotation (rot, gb.getCentreX(), gb.getCentreY()).translated (0.0f, dy));
            letters.addPath (gp);
        }
        letters.applyTransform (juce::AffineTransform::shear (-0.22f, 0.0f));   // écriture penchée

        // Centrage dans le bandeau
        auto lb = letters.getBounds();
        letters.applyTransform (juce::AffineTransform::translation ((float) getWidth() * 0.5f - lb.getCentreX(),
                                                                    h * 0.5f - lb.getCentreY()));
        textBounds = letters.getBounds();

        // Traits épaissis (lettres + contour) pour que le pointillé reste bien lisible.
        juce::Path thick;
        juce::PathStrokeType (h * 0.018f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (thick, letters);

        // 2. Pointillé : on parsème l'intérieur des lettres de points irréguliers (tailles variées).
        const float step = juce::jmax (0.8f, h * 0.036f);
        for (float y = textBounds.getY(); y <= textBounds.getBottom(); y += step)
            for (float x = textBounds.getX(); x <= textBounds.getRight(); x += step)
            {
                const juce::Point<float> p (x + (rng.nextFloat() - 0.5f) * step * 0.9f, y + (rng.nextFloat() - 0.5f) * step * 0.9f);
                if (! letters.contains (p) && ! thick.contains (p))
                    continue;
                if (rng.nextFloat() < 0.10f)          // quelques trous, comme de l'encre qui accroche
                    continue;
                Dot d;
                d.p = p;
                d.r = step * (0.30f + 0.32f * rng.nextFloat());
                d.phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
                d.t = (p.x - textBounds.getX()) / juce::jmax (1.0f, textBounds.getWidth());
                dots.push_back (d);
            }
    }

    void TitleBar::tick()
    {
        time += 1.0f / 30.0f;
        repaint();
    }

    void TitleBar::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();

        // Fond : très légère lueur chaude derrière le titre.
        juce::ColourGradient glow (juce::Colour (0xffff8a5c).withAlpha (0.10f), r.getCentreX(), r.getCentreY(),
                                   juce::Colours::transparentBlack, r.getCentreX() + r.getWidth() * 0.28f, r.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillRect (r);

        // Dégradé chaud (ambre → corail → rose) avec une pointe de la couleur de la famille.
        const juce::Colour c0 (0xffffb347), c1 (0xffff7a59), c2 = juce::Colour (0xffff5f8f).interpolatedWith (accentTint, 0.25f);
        const float breath = 0.5f + 0.5f * std::sin (time * 1.1f);   // « respiration » lente
        for (const auto& d : dots)
        {
            const auto col = d.t < 0.5f ? c0.interpolatedWith (c1, d.t * 2.0f) : c1.interpolatedWith (c2, (d.t - 0.5f) * 2.0f);
            const float twinkle = 0.78f + 0.22f * std::sin (time * 1.7f + d.phase);
            g.setColour (col.withAlpha (juce::jlimit (0.0f, 1.0f, twinkle * (0.85f + 0.15f * breath))));
            const float rr = d.r * (0.92f + 0.08f * breath);
            g.fillEllipse (d.p.x - rr, d.p.y - rr, rr * 2.0f, rr * 2.0f);
        }
    }
}
