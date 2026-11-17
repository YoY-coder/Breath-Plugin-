#include "SpectrumView.h"

namespace breath::ui
{
    namespace
    {
        constexpr float kSpecTopDb = 0.0f, kSpecBottomDb = -84.0f;
        constexpr float kRedScaleDb = 15.0f;       // réduction affichée sur 45 % de la hauteur
        const float gridFreqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };

        juce::String hzText (float f)
        {
            return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz"
                                : juce::String (juce::roundToInt (f)) + " Hz";
        }

        bool readLive (const Slot& s, FrameData& out)
        {
            const auto q = s.live.seq.load (std::memory_order_acquire);
            return q >= 0 && s.live.read (q, INT64_MIN, out);
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
        return a.getY() + 4.0f + db / kRedScaleDb * a.getHeight() * 0.45f;
    }

    float SpectrumView::yToRedDb (float y) const
    {
        auto a = graphArea();
        return (y - a.getY() - 4.0f) / (a.getHeight() * 0.45f) * kRedScaleDb;
    }

    //==========================================================================
    void SpectrumView::refresh()
    {
        uppers.clear();
        if (me < 0)
            return;

        for (int s : hub.hierarchy())
        {
            if (s == me) break;
            uppers.push_back (s);
        }

        auto& sel = proc.selectedUpper;
        if (std::find (uppers.begin(), uppers.end(), sel) == uppers.end())
        {
            sel = -1;
            for (int u : uppers)
                if (hub.getRelation (u, me).enabled) { sel = u; break; }
            if (sel < 0 && ! uppers.empty())
                sel = uppers.front();
        }
        repaint();
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

        for (int u : uppers)
            if (readLive (hub.slot (u), f))
                follow (upper[(size_t) u], f.bands);

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
        {
            const auto mid = (pts[i] + pts[i + 1]) * 0.5f;
            p.quadraticTo (pts[i], mid);
        }
        p.lineTo (pts.back());
        return p;
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

        const int sel = proc.selectedUpper;

        // Zones des relations
        for (int u : uppers)
        {
            const auto rel = hub.getRelation (u, me);
            if (! rel.enabled) continue;
            const auto c = hub.displayColour (u);
            const float x0 = freqToX (juce::jmin (rel.lowHz, rel.highHz)), x1 = freqToX (juce::jmax (rel.lowHz, rel.highHz));
            const auto zone = juce::Rectangle<float> (x0, a.getY() - 8.0f, x1 - x0, a.getHeight() + 8.0f);

            if (u == sel)
            {
                g.setColour (c.withAlpha (0.05f));
                g.fillRect (zone);
            }
            else
            {
                g.setColour (c.withAlpha (0.03f));
                g.fillRect (zone);
                const float dashes[] = { 3.0f, 4.0f };
                g.setColour (c.withAlpha (0.35f));
                for (float x : { x0, x1 })
                    g.drawDashedLine ({ x, zone.getY(), x, zone.getBottom() }, dashes, 2, 1.0f);
            }
        }

        // Spectre de la piste (rempli)
        auto ownPath = curvePath (own.data(), [this] (float db) { return specDbToY (db); });
        {
            auto fill = ownPath;
            fill.lineTo (a.getRight(), a.getBottom());
            fill.lineTo (a.getX(), a.getBottom());
            fill.closeSubPath();
            g.setColour (juce::Colour (0xffe8e8ec).withAlpha (0.10f));
            g.fillPath (fill);
            g.setColour (juce::Colour (0xffe8e8ec).withAlpha (0.75f));
            g.strokePath (ownPath, juce::PathStrokeType (1.5f));
        }

        // Spectres des pistes du dessus
        for (int u : uppers)
        {
            if (! hub.getRelation (u, me).enabled) continue;
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
            for (int d : { 0, 3, 6, 9, 12, 15 })
            {
                const float y = redDbToY ((float) d);
                g.drawText (d == 0 ? juce::String ("0 dB") : "-" + juce::String (d),
                            juce::Rectangle<float> (a.getRight() - 46.0f, d == 0 ? y - 13.0f : y - 6.0f, 40.0f, 12.0f),
                            juce::Justification::centredRight);
            }
        }

        // Poignées de la relation sélectionnée
        if (sel >= 0 && hub.getRelation (sel, me).enabled)
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

            const float hx = (x0 + x1) / 2.0f;
            auto line = [&] (float y, juce::Colour lc, const juce::String& label, std::initializer_list<float> dash)
            {
                std::vector<float> d (dash);
                g.setColour (lc);
                g.drawDashedLine ({ x0, y, x1, y }, d.data(), (int) d.size(), 1.5f);
                g.setColour (juce::Colours::white);
                g.fillEllipse (hx - 6.5f, y - 6.5f, 13.0f, 13.0f);
                g.setColour (lc);
                g.drawEllipse (hx - 6.5f, y - 6.5f, 13.0f, 13.0f, 2.0f);
                const bool roomRight = x1 + 110.0f < a.getRight();
                drawPill (g, { roomRight ? x1 + 8.0f : x0 - 8.0f, y }, label, lc, roomRight ? 1 : -1);
            };

            line (redDbToY (rel.maxReduction), colours::accent,
                  juce::String::fromUTF8 ("max −") + juce::String (rel.maxReduction, 1) + " dB", { 5.0f, 4.0f });
            line (specDbToY (rel.threshold), c,
                  juce::String::fromUTF8 ("seuil ") + juce::String (juce::roundToInt (rel.threshold)) + " dB", { 2.0f, 3.0f });
        }

        // Pastilles : piste courante + pistes du dessus
        chipAreas.clear();
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

            if (uppers.empty())
            {
                g.setColour (colours::text3);
                g.setFont (font (11.0f));
                g.drawText (hub.numActive() <= 1
                                ? juce::String::fromUTF8 ("Ajoutez Breath sur d'autres pistes pour créer des relations")
                                : juce::String::fromUTF8 ("En tête de la hiérarchie : cette piste n'est creusée par aucune autre"),
                            juce::Rectangle<float> (x, cy - 10.0f, 520.0f, 20.0f), juce::Justification::centredLeft);
            }
            else
            {
                drawCaption (g, juce::String::fromUTF8 ("creusée par"), { x, cy - 10.0f, 90.0f, 20.0f });
                x += 84.0f;

                for (int u : uppers)
                {
                    const auto rel = hub.getRelation (u, me);
                    const auto nm = hub.displayName (u);
                    const auto c = hub.displayColour (u);
                    const float w = textWidth (font (12.0f), nm) + 34.0f;
                    if (x + w > (float) getWidth() - 150.0f) break;
                    const auto r = juce::Rectangle<float> (x, cy - 13.0f, w, 26.0f);

                    if (u == sel)
                    {
                        g.setColour (c.withAlpha (0.12f));
                        g.fillRoundedRectangle (r, 7.0f);
                        g.setColour (c.withAlpha (0.55f));
                        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
                    }
                    g.setColour (rel.enabled ? c : c.withAlpha (0.3f));
                    g.fillEllipse (r.getX() + 9.0f, cy - 4.0f, 8.0f, 8.0f);
                    g.setColour (rel.enabled ? (u == sel ? colours::text : colours::text2) : colours::text3);
                    g.setFont (font (12.0f));
                    g.drawText (nm, r.withTrimmedLeft (23.0f), juce::Justification::centredLeft);
                    chipAreas.push_back ({ r, u });
                    x += w + 6.0f;
                }
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
                return { Target::chip, u };

        const auto a = graphArea();
        const int sel = proc.selectedUpper;

        if (sel >= 0 && me >= 0)
        {
            const auto rel = hub.getRelation (sel, me);
            if (rel.enabled)
            {
                const float x0 = freqToX (rel.lowHz), x1 = freqToX (rel.highHz);
                const bool inX = p.x >= x0 - 4.0f && p.x <= x1 + 4.0f;

                if (inX && std::abs (p.y - specDbToY (rel.threshold)) < 7.0f) return { Target::threshold, sel };
                if (inX && std::abs (p.y - redDbToY (rel.maxReduction)) < 7.0f) return { Target::maxLine, sel };
                if (p.y >= a.getY() - 8.0f && p.y <= a.getBottom())
                {
                    if (std::abs (p.x - x0) < 7.0f) return { Target::lowEdge, sel };
                    if (std::abs (p.x - x1) < 7.0f) return { Target::highEdge, sel };
                }
            }
        }

        if (a.contains (p))
            for (int u : uppers)
            {
                if (u == sel) continue;
                const auto rel = hub.getRelation (u, me);
                if (rel.enabled && p.x >= freqToX (rel.lowHz) && p.x <= freqToX (rel.highHz))
                    return { Target::selectOther, u };
            }

        return {};
    }

    void SpectrumView::mouseMove (const juce::MouseEvent& e)
    {
        switch (hitTest (e.position).t)
        {
            case Target::lowEdge:
            case Target::highEdge:   setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
            case Target::maxLine:
            case Target::threshold:  setMouseCursor (juce::MouseCursor::UpDownResizeCursor); break;
            case Target::chip:
            case Target::selectOther: setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
            case Target::none:
            default:                 setMouseCursor (juce::MouseCursor::NormalCursor); break;
        }
    }

    void SpectrumView::mouseDown (const juce::MouseEvent& e)
    {
        drag = hitTest (e.position);
        if (drag.t == Target::chip || drag.t == Target::selectOther)
        {
            proc.selectedUpper = drag.slot;
            drag = {};
            repaint();
            return;
        }
        if (drag.t != Target::none)
        {
            dragStart = hub.getRelation (drag.slot, me);
            dragOrigin = e.position;
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
            case Target::lowEdge:   s.lowHz  = juce::jlimit (kMinFreq, s.highHz / 1.12f, xToFreq (p.x)); break;
            case Target::highEdge:  s.highHz = juce::jlimit (s.lowHz * 1.12f, kMaxFreq, xToFreq (p.x)); break;
            case Target::maxLine:   s.maxReduction = juce::jlimit (0.5f, 24.0f, yToRedDb (p.y)); break;
            case Target::threshold: s.threshold = juce::jlimit (-80.0f, 0.0f, yToSpecDb (p.y)); break;
            case Target::none: case Target::selectOther: case Target::chip: default: break;
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

    void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto h = hitTest (e.position);
        if (h.t == Target::none || h.t == Target::chip || h.t == Target::selectOther)
            return;

        const RelationSettings defaults;
        auto s = hub.getRelation (h.slot, me);
        if (h.t == Target::lowEdge)   s.lowHz = defaults.lowHz;
        if (h.t == Target::highEdge)  s.highHz = defaults.highHz;
        if (h.t == Target::maxLine)   s.maxReduction = defaults.maxReduction;
        if (h.t == Target::threshold) s.threshold = defaults.threshold;
        hub.beginEdit();
        hub.setRelation (h.slot, me, s);
        hub.endEdit();
    }
}
