#include "SpectrumView.h"

namespace breath::ui
{
    namespace
    {
        constexpr float kSpecTopDb = 0.0f, kSpecBottomDb = -84.0f;
        constexpr float kRedScaleSoft = 15.0f;     // réduction affichée sur 45 % de la hauteur (Soft)
        constexpr float kRedScaleHard = 72.0f;     // en Hard, l'échelle s'étend jusqu'à 72 dB
        const float gridFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz"
                                : juce::String (juce::roundToInt (f)) + " Hz";
        }

        juce::String dbText (float v)
        {
            return (v >= 0.0f ? "+" : juce::String::fromUTF8 ("−")) + juce::String (std::abs (v), 1) + " dB";
        }

        bool readLive (const Slot& s, FrameData& out)
        {
            const auto q = s.live.seq.load (std::memory_order_acquire);
            return q >= 0 && s.live.read (q, INT64_MIN, out);
        }

        // Réduction max sans le point « skip » (pour placer un point à la hauteur voulue).
        float maxWithout (const RelationSettings& r, int skip, float f)
        {
            float v = r.maxReduction;
            for (int i = 0; i < r.numNodes; ++i)
                if (i != skip)
                    v += RelationSettings::nodeShape (r.nodes[(size_t) i], f);
            return v;
        }
    }

    SpectrumView::SpectrumView (BreathAudioProcessor& p) : proc (p), hub (p.getHub())
    {
        me = proc.getSlot();
        own.fill (kSilenceDb);
        for (auto& u : upper) u.fill (kSilenceDb);
        refresh();
    }

    //==========================================================================
    juce::Rectangle<float> SpectrumView::graphArea() const
    {
        return getLocalBounds().toFloat().withTrimmedTop (64.0f).withTrimmedBottom (30.0f);
    }

    float SpectrumView::freqToX (float f) const   { auto a = graphArea(); return a.getX() + freqToNorm (f) * a.getWidth(); }
    float SpectrumView::xToFreq (float x) const   { auto a = graphArea(); return normToFreq ((x - a.getX()) / a.getWidth()); }

    float SpectrumView::specDbToY (float db) const
    {
        auto a = graphArea();
        return a.getY() + (kSpecTopDb - juce::jlimit (kSpecBottomDb, kSpecTopDb, db)) / (kSpecTopDb - kSpecBottomDb) * a.getHeight();
    }

    float SpectrumView::yToSpecDb (float y) const
    {
        auto a = graphArea();
        return kSpecTopDb - (y - a.getY()) / a.getHeight() * (kSpecTopDb - kSpecBottomDb);
    }

    float SpectrumView::redDbToY (float db) const
    {
        auto a = graphArea();
        return a.getY() + 4.0f + db / redScale * a.getHeight() * 0.45f;
    }

    float SpectrumView::yToRedDb (float y) const
    {
        auto a = graphArea();
        return (y - a.getY() - 4.0f) / (a.getHeight() * 0.45f) * redScale;
    }

    //==========================================================================
    void SpectrumView::refresh()
    {
        sources.clear();
        if (me < 0)
            return;

        if (hub.isRelationVisible (me, me))
            sources.push_back (me);                       // Self en premier
        for (int s : hub.hierarchy (proc.getFamily()))
            if (s != me && hub.isRelationVisible (s, me))
                sources.push_back (s);

        auto& sel = proc.instUpper;
        if (std::find (sources.begin(), sources.end(), sel) == sources.end())
            sel = sources.empty() ? -1 : sources.front();
        repaint();
    }

    void SpectrumView::select (int source)
    {
        proc.instUpper = source;
        repaint();
        if (onSelectionChange) onSelectionChange();
    }

    void SpectrumView::tick()
    {
        if (me < 0)
            return;

        auto follow = [] (std::array<float, kNumBands>& disp, const float* v)
        {
            for (int b = 0; b < kNumBands; ++b)
                disp[(size_t) b] = v[b] > disp[(size_t) b] ? disp[(size_t) b] + (v[b] - disp[(size_t) b]) * 0.6f
                                                           : disp[(size_t) b] + (v[b] - disp[(size_t) b]) * 0.18f;
        };

        FrameData f;
        if (readLive (hub.slot (me), f))
            follow (own, f.bands);

        for (int u : sources)
            if (u != me && readLive (hub.slot (u), f))
                follow (upper[(size_t) u], f.bands);
        upper[(size_t) me] = own;

        // Échelle de réduction : étendue en mode Hard.
        const float wantScale = (proc.instUpper >= 0 && hub.getRelation (proc.instUpper, me).hard) ? kRedScaleHard : kRedScaleSoft;
        redScale += (wantScale - redScale) * 0.25f;

        auto& curve = hub.slot (me).reductionCurve;
        for (int b = 0; b < kNumBands; ++b)
            reduction[(size_t) b] += (curve[(size_t) b].load (std::memory_order_relaxed) - reduction[(size_t) b]) * 0.5f;

        repaint();
    }

    //==========================================================================
    juce::Path SpectrumView::curvePath (const float* v, std::function<float (float)> toY) const
    {
        auto a = graphArea();
        juce::Path p;
        std::array<juce::Point<float>, kNumBands + 2> pts;
        pts[0] = { a.getX(), toY (v[0]) };
        for (int b = 0; b < kNumBands; ++b)
            pts[(size_t) b + 1] = { freqToX (bandCentre (b)), toY (v[b]) };
        pts[kNumBands + 1] = { a.getRight(), toY (v[kNumBands - 1]) };

        p.startNewSubPath (pts[0]);
        for (size_t i = 1; i + 1 < pts.size(); ++i)
            p.quadraticTo (pts[i], (pts[i] + pts[i + 1]) * 0.5f);
        p.lineTo (pts.back());
        return p;
    }

    float SpectrumView::thresholdAt (const RelationSettings& rel, int src, float x, int skipThrNode) const
    {
        // Décalage des points de façonnage du seuil (sauf « skip », utile pendant qu'on déplace un point).
        float nodes = 0.0f;
        const float f = xToFreq (x);
        for (int i = 0; i < rel.numThrNodes; ++i)
            if (i != skipThrNode)
                nodes += RelationSettings::nodeShape (rel.thrNodes[(size_t) i], f);

        if (! rel.autoThreshold)
            return juce::jlimit (-90.0f, 6.0f, rel.threshold + nodes);
        // Spectre de la source lissé sur ≈ 1 octave (approximation de ce que fait le moteur), + décalage.
        const float bandPos = freqToBand (xToFreq (x));
        const auto& sp = upper[(size_t) juce::jlimit (0, kMaxInstances - 1, src)];
        double acc = 0.0, wsum = 0.0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float dist = std::abs ((float) b - bandPos) / 12.0f;   // 12 bandes fines ≈ 1 octave
            const float w = std::exp (-dist);
            acc += w * sp[(size_t) b];
            wsum += w;
        }
        return juce::jmax (-80.0f, (float) (acc / wsum) + rel.autoOffset + nodes);
    }

    juce::Point<float> SpectrumView::kneeHandlePos (const RelationSettings& rel, int src) const
    {
        // Poignée du knee : près du bord gauche de la zone, sur le bord haut de la bande du knee.
        const float x = juce::jmin (freqToX (rel.lowHz) + 22.0f, (freqToX (rel.lowHz) + freqToX (rel.highHz)) * 0.5f - 16.0f);
        return { x, specDbToY (thresholdAt (rel, src, x) + 0.5f * rel.knee) - 8.0f };
    }

    void SpectrumView::drawPill (juce::Graphics& g, juce::Point<float> anchor, const juce::String& text, juce::Colour c, int align) const
    {
        const auto f = font (11.0f, Weight::semibold);
        const float w = textWidth (f, text) + 14.0f;
        float x = align < 0 ? anchor.x - w : (align == 0 ? anchor.x - w / 2.0f : anchor.x);
        x = juce::jlimit (2.0f, (float) getWidth() - w - 2.0f, x);
        const auto r = juce::Rectangle<float> (x, anchor.y - 10.0f, w, 20.0f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (f);
        g.drawText (text, r, juce::Justification::centred, false);
    }

    void SpectrumView::paint (juce::Graphics& g)
    {
        auto bounds = getLocalBounds().toFloat();
        drawCard (g, bounds);

        if (me < 0)
        {
            g.setColour (colours::text2);
            g.setFont (font (14.0f));
            g.drawText (juce::String::fromUTF8 ("Limite de 16 instances atteinte : cette instance laisse passer le son sans traitement."),
                        bounds, juce::Justification::centred);
            return;
        }

        auto a = graphArea();
        juce::Graphics::ScopedSaveState clipAll (g);
        g.reduceClipRegion (bounds.reduced (1.0f).toNearestInt());

        // Grille et fréquences
        g.setFont (font (10.0f));
        for (float f : gridFreqs)
        {
            const float x = freqToX (f);
            g.setColour (juce::Colour (0xff2a2a2f));
            g.fillRect (x, a.getY() - 6.0f, 1.0f, a.getHeight() + 6.0f);
            g.setColour (colours::text3);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (x - 20.0f, a.getBottom() + 6.0f, 40.0f, 16.0f), juce::Justification::centred);
        }

        const int sel = proc.instUpper;

        // Zones des relations (bords en fondu selon la pente réglée)
        for (int u : sources)
        {
            const auto rel = hub.getRelation (u, me);
            if (! rel.enabled && u != sel) continue;          // relation en bypass : pas de zone
            const auto c = rel.enabled ? hub.displayColour (u) : colours::text3;
            const float alpha = u == sel ? 0.07f : 0.035f;
            const float top = a.getY() - 8.0f, h = a.getHeight() + 8.0f;
            // Pondération réelle de la zone (bords en fondu) dessinée en dégradé continu.
            juce::ColourGradient grad (c.withAlpha (0.0f), a.getX(), 0.0f, c.withAlpha (0.0f), a.getRight(), 0.0f, false);
            const int stops = 96;
            for (int i = 1; i < stops; ++i)
            {
                const float t = (float) i / (float) stops;
                grad.addColour (t, c.withAlpha (alpha * rel.rangeWeight (xToFreq (a.getX() + t * a.getWidth()))));
            }
            g.setGradientFill (grad);
            g.fillRect (juce::Rectangle<float> (a.getX(), top, a.getWidth(), h));
            if (u != sel)
            {
                const float dashes[] = { 3.0f, 4.0f };
                g.setColour (c.withAlpha (0.35f));
                for (float x : { freqToX (rel.lowHz), freqToX (rel.highHz) })
                    g.drawDashedLine ({ x, top, x, top + h }, dashes, 2, 1.0f);
            }
        }

        // Spectre de la piste (rempli)
        {
            auto ownPath = curvePath (own.data(), [this] (float db) { return specDbToY (db); });
            auto fill = ownPath;
            fill.lineTo (a.getRight(), a.getBottom());
            fill.lineTo (a.getX(), a.getBottom());
            fill.closeSubPath();
            g.setColour (juce::Colour (0xffe8e8ec).withAlpha (0.10f));
            g.fillPath (fill);
            g.setColour (juce::Colour (0xffe8e8ec).withAlpha (0.75f));
            g.strokePath (ownPath, juce::PathStrokeType (1.5f));
        }

        // Spectres des pistes sources
        for (int u : sources)
        {
            if (u == me || ! hub.getRelation (u, me).enabled) continue;
            g.setColour (hub.displayColour (u).withAlpha (u == sel ? 0.9f : 0.5f));
            g.strokePath (curvePath (upper[(size_t) u].data(), [this] (float db) { return specDbToY (db); }), juce::PathStrokeType (1.5f));
        }

        // Courbe de réduction
        {
            const float zeroY = redDbToY (0.0f);
            auto red = curvePath (reduction.data(), [this] (float db) { return redDbToY (db); });
            auto fill = red;
            fill.lineTo (a.getRight(), zeroY);
            fill.lineTo (a.getX(), zeroY);
            fill.closeSubPath();
            g.setColour (colours::accent.withAlpha (0.18f));
            g.fillPath (fill);
            const float dashes[] = { 2.0f, 3.0f };
            g.setColour (colours::accent.withAlpha (0.35f));
            g.drawDashedLine ({ a.getX(), zeroY, a.getRight(), zeroY }, dashes, 2, 1.0f);
            g.setColour (colours::accent);
            g.strokePath (red, juce::PathStrokeType (2.0f));

            g.setFont (font (9.0f));
            g.setColour (colours::accent.withAlpha (0.6f));
            const bool hardScale = redScale > 30.0f;
            for (int d : hardScale ? std::initializer_list<int> { 0, 12, 24, 36, 48, 60, 72 } : std::initializer_list<int> { 0, 3, 6, 9, 12, 15 })
            {
                const float y = redDbToY ((float) d);
                g.drawText (d == 0 ? juce::String ("0 dB") : "-" + juce::String (d),
                            juce::Rectangle<float> (a.getRight() - 46.0f, d == 0 ? y - 13.0f : y - 6.0f, 40.0f, 12.0f),
                            juce::Justification::centredRight);
            }
        }

        // Poignées de la relation sélectionnée
        if (sel >= 0)
        {
            const auto rel = hub.getRelation (sel, me);
            const auto c = hub.displayColour (sel);
            const float x0 = freqToX (rel.lowHz), x1 = freqToX (rel.highHz);
            const float midY = a.getCentreY();

            for (float x : { x0, x1 })
            {
                g.setColour (c);
                g.fillRect (x - 0.75f, a.getY() - 8.0f, 1.5f, a.getHeight() + 8.0f);
                g.fillRoundedRectangle (x - 4.0f, midY - 16.0f, 8.0f, 32.0f, 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.drawLine (x, midY - 8.0f, x, midY + 8.0f, 1.5f);
            }
            drawPill (g, { x0 + 6.0f, a.getBottom() - 14.0f }, hzText (rel.lowHz), c.withAlpha (0.9f), 1);
            drawPill (g, { x1 - 6.0f, a.getBottom() - 14.0f }, hzText (rel.highHz), c.withAlpha (0.9f), -1);

            // Pente : visible au survol / pendant le réglage d'un bord.
            auto slopeInfo = [&] (bool low)
            {
                const float w = low ? rel.lowSlope : rel.highSlope;
                const float f = low ? rel.lowHz : rel.highHz;
                const float xa = freqToX (f / std::pow (2.0f, w * 0.5f)), xb = freqToX (f * std::pow (2.0f, w * 0.5f));
                const float d[] = { 2.0f, 3.0f };
                g.setColour (c.withAlpha (0.6f));
                g.drawDashedLine ({ xa, a.getY(), xa, a.getBottom() }, d, 2, 1.0f);
                g.drawDashedLine ({ xb, a.getY(), xb, a.getBottom() }, d, 2, 1.0f);
                drawPill (g, { low ? x0 : x1, midY + 30.0f },
                          "pente " + juce::String (juce::roundToInt (RelationSettings::slopeToDbPerOct (w))) + " dB/oct", c, 0);
            };
            const bool lowHot  = (hover.t == Target::lowEdge)  || (drag.t == Target::lowEdge);
            const bool highHot = (hover.t == Target::highEdge) || (drag.t == Target::highEdge);
            if (lowHot)  slopeInfo (true);
            if (highHot) slopeInfo (false);

            const float hx = (x0 + x1) / 2.0f;
            const bool roomRight = x1 + 110.0f < a.getRight();

            // Seuil (fixe : ligne ; auto : courbe qui suit le spectre moyen de la source)
            {
                const float d[] = { 2.0f, 3.0f };
                juce::Path thrPath;
                const int steps = 80;
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = x0 + (x1 - x0) * (float) i / (float) steps;
                    const float y = specDbToY (thresholdAt (rel, sel, x));
                    if (i == 0) thrPath.startNewSubPath (x, y); else thrPath.lineTo (x, y);
                }
                // Bande du knee : zone où la réduction démarre progressivement.
                if (rel.knee > 0.05f)
                {
                    juce::Path band;
                    for (int i = 0; i <= steps; ++i)
                    {
                        const float x = x0 + (x1 - x0) * (float) i / (float) steps;
                        const float y = specDbToY (thresholdAt (rel, sel, x) + 0.5f * rel.knee);
                        if (i == 0) band.startNewSubPath (x, y); else band.lineTo (x, y);
                    }
                    for (int i = steps; i >= 0; --i)
                    {
                        const float x = x0 + (x1 - x0) * (float) i / (float) steps;
                        band.lineTo (x, specDbToY (thresholdAt (rel, sel, x) - 0.5f * rel.knee));
                    }
                    band.closeSubPath();
                    g.setColour (c.withAlpha (0.10f));
                    g.fillPath (band);
                }

                juce::Path dashed;
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, thrPath, d, 2);
                g.setColour (c);
                g.fillPath (dashed);

                // Points du seuil
                for (int i = 0; i < rel.numThrNodes; ++i)
                {
                    const auto& n = rel.thrNodes[(size_t) i];
                    const auto pt = juce::Point<float> (freqToX (n.freq), specDbToY (thresholdAt (rel, sel, freqToX (n.freq))));
                    const bool hot = (hover.t == Target::thrNode && hover.node == i) || (drag.t == Target::thrNode && drag.node == i);
                    const float xl = freqToX (n.freq / std::pow (2.0f, n.width * 0.5f)), xr = freqToX (n.freq * std::pow (2.0f, n.width * 0.5f));
                    g.setColour (c.withAlpha (hot ? 0.20f : 0.09f));
                    g.fillRoundedRectangle (xl, pt.y - 3.0f, xr - xl, 6.0f, 3.0f);
                    g.setColour (c);
                    g.fillEllipse (pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f);
                    g.setColour (juce::Colour (0xff141416));
                    g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
                    if (hot)
                        drawPill (g, { pt.x, pt.y + 20.0f }, hzText (n.freq) + juce::String::fromUTF8 (" · ") + dbText (n.gain), c, 0);
                }

                // Poignée du knee (seuil « soft »)
                {
                    const auto kp = kneeHandlePos (rel, sel);
                    const bool hot = hover.t == Target::knee || drag.t == Target::knee;
                    auto kr = juce::Rectangle<float> (16.0f, 10.0f).withCentre (kp);
                    g.setColour (hot ? c : c.withAlpha (0.7f));
                    g.fillRoundedRectangle (kr, 4.0f);
                    g.setColour (juce::Colour (0xff141416));
                    g.drawLine (kr.getX() + 4.0f, kr.getCentreY() - 1.5f, kr.getRight() - 4.0f, kr.getCentreY() - 1.5f, 1.0f);
                    g.drawLine (kr.getX() + 4.0f, kr.getCentreY() + 1.5f, kr.getRight() - 4.0f, kr.getCentreY() + 1.5f, 1.0f);
                    if (hot)
                        drawPill (g, { kp.x, kp.y - 16.0f }, "knee " + juce::String (rel.knee, 1) + " dB", c, 0);
                }

                const float y = specDbToY (thresholdAt (rel, sel, hx));
                g.setColour (juce::Colours::white);
                g.fillEllipse (hx - 6.5f, y - 6.5f, 13.0f, 13.0f);
                g.setColour (c);
                g.drawEllipse (hx - 6.5f, y - 6.5f, 13.0f, 13.0f, 2.0f);
                const auto label = rel.autoThreshold
                    ? juce::String::fromUTF8 ("seuil auto ") + (rel.autoOffset >= 0.0f ? "+" : "") + juce::String (rel.autoOffset, 1) + " dB"
                    : juce::String::fromUTF8 ("seuil ") + juce::String (juce::roundToInt (rel.threshold)) + " dB";
                drawPill (g, { roomRight ? x1 + 8.0f : x0 - 8.0f, specDbToY (thresholdAt (rel, sel, roomRight ? x1 : x0)) }, label, c, roomRight ? 1 : -1);
            }

            // Courbe de réduction max (façonnable par points)
            {
                juce::Path maxPath;
                const int steps = 120;
                for (int i = 0; i <= steps; ++i)
                {
                    const float x = x0 + (x1 - x0) * (float) i / (float) steps;
                    const float y = redDbToY (rel.maxAt (xToFreq (x)));
                    if (i == 0) maxPath.startNewSubPath (x, y); else maxPath.lineTo (x, y);
                }
                juce::Path dashed;
                const float d[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (dashed, maxPath, d, 2);
                g.setColour (colours::accent);
                g.fillPath (dashed);

                const float baseY = redDbToY (rel.maxAt (xToFreq (hx)));
                g.setColour (juce::Colours::white);
                g.fillEllipse (hx - 6.5f, baseY - 6.5f, 13.0f, 13.0f);
                g.setColour (colours::accent);
                g.drawEllipse (hx - 6.5f, baseY - 6.5f, 13.0f, 13.0f, 2.0f);
                drawPill (g, { roomRight ? x1 + 8.0f : x0 - 8.0f, redDbToY (rel.maxAt (rel.highHz)) },
                          juce::String::fromUTF8 ("max −") + juce::String (rel.maxReduction, 1) + " dB", colours::accent, roomRight ? 1 : -1);

                for (int i = 0; i < rel.numNodes; ++i)
                {
                    const auto& n = rel.nodes[(size_t) i];
                    const auto pt = juce::Point<float> (freqToX (n.freq), redDbToY (rel.maxAt (n.freq)));
                    const bool hot = (hover.t == Target::node && hover.node == i) || (drag.t == Target::node && drag.node == i);
                    // largeur du point
                    g.setColour (colours::accent.withAlpha (hot ? 0.18f : 0.08f));
                    const float xl = freqToX (n.freq / std::pow (2.0f, n.width * 0.5f)), xr = freqToX (n.freq * std::pow (2.0f, n.width * 0.5f));
                    g.fillRoundedRectangle (xl, pt.y - 3.0f, xr - xl, 6.0f, 3.0f);
                    g.setColour (colours::accent);
                    g.fillEllipse (pt.x - 6.0f, pt.y - 6.0f, 12.0f, 12.0f);
                    g.setColour (juce::Colours::white);
                    g.fillEllipse (pt.x - 2.5f, pt.y - 2.5f, 5.0f, 5.0f);
                    if (hot)
                        drawPill (g, { pt.x, pt.y + 20.0f }, hzText (n.freq) + juce::String::fromUTF8 (" · ") + dbText (n.gain), colours::accent, 0);
                }
            }

            // Aide discrète (au survol seulement)
            if (isMouseOver (true))
            {
            g.setFont (font (10.0f));
            g.setColour (colours::text3);
            g.drawText (juce::String::fromUTF8 ("Bords : ↔ fréquence, ↕ pente · courbes bleue (max) et pointillée (seuil) : double-clic = point, molette = largeur · poignée ≡ = knee"),
                        juce::Rectangle<float> (a.getX() + 12.0f, a.getBottom() - 40.0f, a.getWidth() - 24.0f, 14.0f),
                        juce::Justification::centredLeft);
            }
        }

        // Pastilles : piste courante + sources
        chipAreas.clear();
        addChipArea = {};
        {
            float x = 18.0f;
            const float cy = 30.0f;
            g.setFont (font (12.0f));
            g.setColour (juce::Colour (0xffe8e8ec));
            g.fillEllipse (x, cy - 4.0f, 8.0f, 8.0f);
            g.setColour (colours::text2);
            const auto myName = hub.displayName (me);
            g.drawText (myName, juce::Rectangle<float> (x + 14.0f, cy - 10.0f, 200.0f, 20.0f), juce::Justification::centredLeft);
            x += 14.0f + textWidth (font (12.0f), myName) + 18.0f;

            drawCaption (g, juce::String::fromUTF8 ("creusée par"), { x, cy - 10.0f, 90.0f, 20.0f });
            x += 84.0f;

            // Pastilles de couleur (une par piste source, + Self) : clic = choisir la relation réglée.
            for (int u : sources)
            {
                const auto rel = hub.getRelation (u, me);
                const bool isSelf = u == me;
                const auto nm = isSelf ? juce::String ("Self") : hub.displayName (u);
                const auto c = isSelf ? juce::Colour (0xffe8e8ec) : hub.displayColour (u);
                const float w = textWidth (font (12.0f, Weight::medium), nm) + 34.0f;
                if (x + w > (float) getWidth() - 190.0f) break;
                const auto r = juce::Rectangle<float> (x, cy - 13.0f, w, 26.0f);
                const bool on = rel.enabled;

                if (u == sel)
                {
                    g.setColour (on ? c : colours::raised);
                    g.fillRoundedRectangle (r, 13.0f);
                    g.setColour (on ? juce::Colour (0xff141416) : colours::text2);
                }
                else
                {
                    g.setColour ((on ? c : colours::text3).withAlpha (on ? 0.7f : 0.5f));
                    g.drawRoundedRectangle (r.reduced (0.75f), 13.0f, 1.5f);
                    g.setColour (on ? c.withAlpha (0.85f) : colours::text3);
                }
                g.fillEllipse (r.getX() + 10.0f, cy - 3.5f, 7.0f, 7.0f);
                g.setFont (font (12.0f, Weight::medium));
                g.drawText (on ? nm : nm + " (off)", r.withTrimmedLeft (23.0f), juce::Justification::centredLeft);
                chipAreas.push_back ({ r, u });
                x += w + 6.0f;
            }

            {
                addChipArea = { x, cy - 13.0f, 26.0f, 26.0f };
                g.setColour (hover.t == Target::addChip ? colours::raised : colours::panel2);
                g.fillRoundedRectangle (addChipArea, 7.0f);
                g.setColour (colours::text2);
                g.drawLine (addChipArea.getCentreX() - 5.0f, cy, addChipArea.getCentreX() + 5.0f, cy, 1.5f);
                g.drawLine (addChipArea.getCentreX(), cy - 5.0f, addChipArea.getCentreX(), cy + 5.0f, 1.5f);
                x += 34.0f;
            }

            if (sources.empty())
            {
                g.setColour (colours::text3);
                g.setFont (font (11.0f));
                g.drawText (hub.numActive (proc.getFamily()) <= 1
                                ? juce::String::fromUTF8 ("Ajoutez Breath sur d'autres pistes pour créer des relations")
                                : juce::String::fromUTF8 ("Aucune piste ne creuse celle-ci : « + » pour en ajouter une (ou Self)"),
                            juce::Rectangle<float> (x, cy - 10.0f, 360.0f, 20.0f), juce::Justification::centredLeft);
            }
        }

        // Réduction actuelle
        float maxRed = 0.0f;
        for (auto v : reduction) maxRed = juce::jmax (maxRed, v);
        drawCaption (g, juce::String::fromUTF8 ("réduction actuelle"), { bounds.getRight() - 200.0f, 12.0f, 182.0f, 14.0f }, juce::Justification::centredRight);
        g.setColour (colours::accent);
        g.setFont (font (22.0f, Weight::semibold));
        g.drawText (juce::String::fromUTF8 ("−") + juce::String (maxRed, 1) + " dB",
                    juce::Rectangle<float> (bounds.getRight() - 200.0f, 26.0f, 182.0f, 28.0f), juce::Justification::centredRight);
    }

    //==========================================================================
    SpectrumView::Hit SpectrumView::hitTest (juce::Point<float> p) const
    {
        for (auto& [r, u] : chipAreas)
            if (r.contains (p))
                return { Target::chip, u, -1 };
        if (addChipArea.contains (p))
            return { Target::addChip, -1, -1 };

        const auto a = graphArea();
        const int sel = proc.instUpper;

        if (sel >= 0 && me >= 0)
        {
            const auto rel = hub.getRelation (sel, me);
            for (int i = 0; i < rel.numNodes; ++i)
            {
                const auto& n = rel.nodes[(size_t) i];
                if (p.getDistanceFrom ({ freqToX (n.freq), redDbToY (rel.maxAt (n.freq)) }) < 9.0f)
                    return { Target::node, sel, i };
            }

            for (int i = 0; i < rel.numThrNodes; ++i)
            {
                const float nx = freqToX (rel.thrNodes[(size_t) i].freq);
                if (p.getDistanceFrom ({ nx, specDbToY (thresholdAt (rel, sel, nx)) }) < 9.0f)
                    return { Target::thrNode, sel, i };
            }
            if (p.getDistanceFrom (kneeHandlePos (rel, sel)) < 9.0f)
                return { Target::knee, sel, -1 };

            const float x0 = freqToX (rel.lowHz), x1 = freqToX (rel.highHz);
            const bool inX = p.x >= x0 - 4.0f && p.x <= x1 + 4.0f;

            if (inX && std::abs (p.y - specDbToY (thresholdAt (rel, sel, p.x))) < 7.0f) return { Target::threshold, sel, -1 };
            if (inX && std::abs (p.y - redDbToY (rel.maxAt (xToFreq (p.x)))) < 7.0f) return { Target::maxLine, sel, -1 };
            if (p.y >= a.getY() - 8.0f && p.y <= a.getBottom())
            {
                if (std::abs (p.x - x0) < 7.0f) return { Target::lowEdge, sel, -1 };
                if (std::abs (p.x - x1) < 7.0f) return { Target::highEdge, sel, -1 };
            }
        }

        return {};
    }

    void SpectrumView::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = hitTest (e.position);
        if (h.t != hover.t || h.node != hover.node) { hover = h; repaint(); }

        switch (h.t)
        {
            case Target::lowEdge:
            case Target::highEdge:    setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Target::maxLine:
            case Target::threshold:   setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Target::node:
            case Target::thrNode:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
            case Target::knee:        setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Target::chip:
            case Target::addChip:
            case Target::selectOther: setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
            case Target::none:
            default:                  setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    void SpectrumView::mouseExit (const juce::MouseEvent&)
    {
        hover = {};
        repaint();
    }

    void SpectrumView::showAddMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader (juce::String::fromUTF8 ("Faire creuser cette piste par…"));
        if (! hub.isRelationVisible (me, me))
            m.addItem (1000, juce::String::fromUTF8 ("Self (la piste se creuse elle-même : compresseur / anti-résonances)"));
        for (int s : hub.hierarchy (proc.getFamily()))
            if (s != me && ! hub.isRelationVisible (s, me))
                m.addItem (s + 1, hub.displayName (s));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (addChipArea.toNearestInt())),
                         [this] (int r)
                         {
                             const int s = r == 1000 ? me : r - 1;
                             if (r <= 0 || ! hub.isActive (s)) return;
                             auto rel = hub.getRelation (s, me);
                             rel.assigned = true;
                             rel.enabled = true;
                             rel.followOrder = s != me && hub.isAbove (s, me);
                             hub.beginEdit();
                             hub.setRelation (s, me, rel);
                             hub.endEdit();
                             select (s);
                         });
    }

    void SpectrumView::showSourceMenu (int source)
    {
        const auto rel = hub.getRelation (source, me);
        const auto nm = source == me ? juce::String ("Self") : hub.displayName (source);
        juce::PopupMenu m;
        m.addItem (1, rel.enabled ? juce::String::fromUTF8 ("Bypass de la relation ") + nm : juce::String::fromUTF8 ("Réactiver la relation ") + nm);
        m.addItem (2, juce::String::fromUTF8 ("Retirer la relation ") + nm);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, source] (int r)
        {
            if (! hub.isActive (source)) return;
            auto s = hub.getRelation (source, me);
            if (r == 1) s.enabled = ! s.enabled;
            else if (r == 2) s.assigned = false;
            else return;
            hub.beginEdit();
            hub.setRelation (source, me, s);
            hub.endEdit();
        });
    }

    void SpectrumView::mouseDown (const juce::MouseEvent& e)
    {
        drag = hitTest (e.position);

        if (drag.t == Target::addChip) { drag = {}; showAddMenu(); return; }

        if (drag.t == Target::chip)
        {
            const int s = drag.slot;
            drag = {};
            select (s);
            if (e.mods.isPopupMenu()) showSourceMenu (s);
            return;
        }

        if (drag.t == Target::thrNode && e.mods.isPopupMenu())
        {
            auto s = hub.getRelation (drag.slot, me);
            const int idx = drag.node;
            drag = {};
            juce::PopupMenu m;
            m.addItem (1, "Supprimer le point du seuil");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, s, idx] (int r) mutable
            {
                if (r != 1 || idx >= s.numThrNodes) return;
                for (int i = idx; i < s.numThrNodes - 1; ++i) s.thrNodes[(size_t) i] = s.thrNodes[(size_t) i + 1];
                --s.numThrNodes;
                hub.beginEdit(); hub.setRelation (proc.instUpper, me, s); hub.endEdit();
            });
            return;
        }

        if (drag.t == Target::node && e.mods.isPopupMenu())
        {
            auto s = hub.getRelation (drag.slot, me);
            const int idx = drag.node;
            drag = {};
            juce::PopupMenu m;
            m.addItem (1, "Supprimer le point");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, s, idx] (int r) mutable
            {
                if (r != 1 || idx >= s.numNodes) return;
                for (int i = idx; i < s.numNodes - 1; ++i) s.nodes[(size_t) i] = s.nodes[(size_t) i + 1];
                --s.numNodes;
                hub.beginEdit(); hub.setRelation (proc.instUpper, me, s); hub.endEdit();
            });
            return;
        }

        if (drag.t != Target::none)
        {
            dragStart = hub.getRelation (drag.slot, me);
            dragOrigin = e.position;
            dragAxis = 0;
            hub.beginEdit();
        }
    }

    void SpectrumView::mouseDrag (const juce::MouseEvent& e)
    {
        if (drag.t == Target::none)
            return;

        // Alt / Option : réglage fin.
        const float fine = e.mods.isAltDown() ? 0.2f : 1.0f;
        const auto p = dragOrigin + (e.position - dragOrigin) * fine;
        auto s = dragStart;

        switch (drag.t)
        {
            case Target::lowEdge:
            case Target::highEdge:
            {
                // Comme sur un séparateur de bandes : horizontal = fréquence, vertical = pente.
                const auto delta = e.position - dragOrigin;
                if (dragAxis == 0 && delta.getDistanceFromOrigin() > 4.0f)
                    dragAxis = std::abs (delta.x) >= std::abs (delta.y) ? 1 : 2;

                const bool low = drag.t == Target::lowEdge;
                if (dragAxis == 1)
                {
                    if (low) s.lowHz  = juce::jlimit (kMinFreq, s.highHz / 1.12f, xToFreq (p.x));
                    else     s.highHz = juce::jlimit (s.lowHz * 1.12f, kMaxFreq, xToFreq (p.x));
                }
                else if (dragAxis == 2)
                {
                    // Vers le haut = pente plus raide (fondu plus court), vers le bas = plus douce.
                    const float startW = low ? dragStart.lowSlope : dragStart.highSlope;
                    const float w = juce::jlimit (0.05f, 4.0f, startW * std::pow (2.0f, (p.y - dragOrigin.y) / 40.0f));
                    (low ? s.lowSlope : s.highSlope) = w;
                }
                break;
            }
            case Target::maxLine:   s.maxReduction = juce::jlimit (0.5f, s.maxLimit(), dragStart.maxReduction + yToRedDb (p.y) - yToRedDb (dragOrigin.y)); break;
            case Target::threshold:
                if (s.autoThreshold) s.autoOffset = juce::jlimit (-24.0f, 24.0f, dragStart.autoOffset + yToSpecDb (p.y) - yToSpecDb (dragOrigin.y));
                else                 s.threshold = juce::jlimit (-80.0f, 0.0f, yToSpecDb (p.y));
                break;
            case Target::thrNode:
            {
                auto& n = s.thrNodes[(size_t) drag.node];
                n.freq = juce::jlimit (kMinFreq, kMaxFreq, xToFreq (p.x));
                const float without = thresholdAt (s, drag.slot, freqToX (n.freq), drag.node);
                n.gain = juce::jlimit (-40.0f, 40.0f, yToSpecDb (p.y) - without);
                break;
            }
            case Target::knee:
                // Vers le haut = knee plus large (seuil plus doux).
                s.knee = juce::jlimit (0.0f, 24.0f, dragStart.knee + 2.0f * (yToSpecDb (p.y) - yToSpecDb (dragOrigin.y)));
                break;
            case Target::node:
            {
                auto& n = s.nodes[(size_t) drag.node];
                n.freq = juce::jlimit (kMinFreq, kMaxFreq, xToFreq (p.x));
                n.gain = juce::jlimit (-30.0f, 30.0f, yToRedDb (p.y) - maxWithout (s, drag.node, n.freq));
                break;
            }
            case Target::none: case Target::selectOther: case Target::chip: case Target::addChip: default: break;
        }
        hub.setRelation (drag.slot, me, s);
        repaint();
    }

    void SpectrumView::mouseUp (const juce::MouseEvent&)
    {
        if (drag.t != Target::none)
            hub.endEdit();
        drag = {};
    }

    void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        const auto h = hitTest (e.position);
        if (h.t != Target::node && h.t != Target::thrNode)
            return;
        auto s = hub.getRelation (h.slot, me);
        auto& n = h.t == Target::node ? s.nodes[(size_t) h.node] : s.thrNodes[(size_t) h.node];
        n.width = juce::jlimit (0.1f, 6.0f, n.width * (w.deltaY > 0 ? 1.12f : 1.0f / 1.12f));
        hub.beginEdit();
        hub.setRelation (h.slot, me, s);
        hub.endEdit();
    }

    void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto h = hitTest (e.position);
        const int sel = proc.instUpper;
        if (sel < 0)
            return;

        auto s = hub.getRelation (sel, me);
        const RelationSettings defaults;

        if (h.t == Target::node)
        {
            for (int i = h.node; i < s.numNodes - 1; ++i)
                s.nodes[(size_t) i] = s.nodes[(size_t) i + 1];
            --s.numNodes;
        }
        else if (h.t == Target::maxLine)
        {
            if (s.numNodes >= kMaxNodes)
                return;
            ShapeNode n;
            n.freq = juce::jlimit (kMinFreq, kMaxFreq, xToFreq (e.position.x));
            n.width = 1.0f;
            n.gain = 0.0f;
            s.nodes[(size_t) s.numNodes++] = n;
        }
        else if (h.t == Target::lowEdge)   { s.lowHz = defaults.lowHz;   s.lowSlope = defaults.lowSlope; }
        else if (h.t == Target::highEdge)  { s.highHz = defaults.highHz; s.highSlope = defaults.highSlope; }
        else if (h.t == Target::thrNode)
        {
            for (int i = h.node; i < s.numThrNodes - 1; ++i)
                s.thrNodes[(size_t) i] = s.thrNodes[(size_t) i + 1];
            --s.numThrNodes;
        }
        else if (h.t == Target::knee) { s.knee = 0.0f; }
        else if (h.t == Target::threshold)
        {
            const float hx = (freqToX (s.lowHz) + freqToX (s.highHz)) * 0.5f;
            if (std::abs (e.position.x - hx) < 9.0f)
            {
                // Double-clic sur la poignée centrale : seuil par défaut.
                s.threshold = defaults.threshold;
                s.autoOffset = defaults.autoOffset;
            }
            else
            {
                // Double-clic ailleurs sur la courbe de seuil : nouveau point.
                if (s.numThrNodes >= kMaxNodes) return;
                s.thrNodes[(size_t) s.numThrNodes++] = { juce::jlimit (kMinFreq, kMaxFreq, xToFreq (e.position.x)), 0.0f, 1.0f };
            }
        }
        else if (h.t == Target::none && graphArea().contains (e.position)
                 && e.position.x >= freqToX (s.lowHz) && e.position.x <= freqToX (s.highHz)
                 && e.position.y < redDbToY (24.0f) && s.numNodes < kMaxNodes)
        {
            // Double-clic près de la courbe max : nouveau point à cette hauteur.
            ShapeNode n;
            n.freq = xToFreq (e.position.x);
            n.width = 1.0f;
            n.gain = juce::jlimit (-30.0f, 30.0f, yToRedDb (e.position.y) - s.maxAt (n.freq));
            s.nodes[(size_t) s.numNodes++] = n;
        }
        else
            return;

        hub.beginEdit();
        hub.setRelation (sel, me, s);
        hub.endEdit();
    }
}
