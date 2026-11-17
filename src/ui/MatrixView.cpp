#include "MatrixView.h"
#include "InstanceView.h"

namespace breath::ui
{
    namespace
    {
        constexpr float kRowH = 44.0f, kListTop = 32.0f;

        float maxReductionOf (const Slot& s)
        {
            float m = 0.0f;
            for (auto& v : s.reductionCurve)
                m = juce::jmax (m, v.load (std::memory_order_relaxed));
            return m;
        }

        // Sélection par défaut cohérente (lower = cette instance si possible).
        void ensureSelection (BreathAudioProcessor& proc)
        {
            auto& hub = proc.getHub();
            const auto order = hub.hierarchy();
            auto valid = [&] (int u, int l)
            {
                return hub.isActive (u) && hub.isActive (l) && u != l && hub.isAbove (u, l);
            };
            if (valid (proc.selectedUpper, proc.selectedLower))
                return;

            const int me = proc.getSlot();
            const int lower = (hub.isActive (proc.selectedLower) && proc.selectedLower != order.front()) ? proc.selectedLower
                            : (me >= 0 && ! order.empty() && me != order.front() ? me : (order.size() > 1 ? order[1] : -1));
            proc.selectedLower = lower;
            proc.selectedUpper = -1;
            if (lower >= 0)
                for (int s : order)
                {
                    if (s == lower) break;
                    proc.selectedUpper = s;
                    if (hub.getRelation (s, lower).enabled) break;
                }
        }
    }

    //==========================================================================
    HierarchyList::HierarchyList (BreathAudioProcessor& p) : proc (p), hub (p.getHub()) { refresh(); }

    void HierarchyList::refresh()
    {
        if (dragIndex < 0)
            order = hub.hierarchy();
        repaint();
    }

    void HierarchyList::tick()
    {
        for (int s : order)
            meter[(size_t) s] += (maxReductionOf (hub.slot (s)) - meter[(size_t) s]) * 0.3f;
        repaint();
    }

    juce::Rectangle<float> HierarchyList::rowArea (int i) const
    {
        return { 8.0f, kListTop + kRowH * (float) i, (float) getWidth() - 16.0f, kRowH };
    }

    juce::Rectangle<float> HierarchyList::starArea (int i) const
    {
        auto r = rowArea (i);
        return { r.getRight() - 52.0f, r.getY(), 22.0f, r.getHeight() };
    }

    void HierarchyList::paint (juce::Graphics& g)
    {
        drawCaption (g, juce::String::fromUTF8 ("Hiérarchie"), { 16.0f, 8.0f, 200.0f, 16.0f });
        const int master = hub.getMaster();
        const int me = proc.getSlot();

        auto drawRow = [&] (int i, juce::Rectangle<float> r, bool floating)
        {
            const int s = order[(size_t) i];
            if (floating)
            {
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillRoundedRectangle (r.translated (0.0f, 6.0f).expanded (2.0f), 10.0f);
                g.setColour (juce::Colour (0xff2c2c31));
                g.fillRoundedRectangle (r, 9.0f);
            }
            else if (s == proc.selectedLower || s == me)
            {
                g.setColour (s == proc.selectedLower ? colours::panel2 : colours::panel2.withAlpha (0.5f));
                g.fillRoundedRectangle (r, 9.0f);
            }

            auto rr = r.reduced (10.0f, 0.0f);
            g.setFont (font (11.0f));
            g.setColour (colours::text3);
            g.drawText (juce::String (i + 1), rr.removeFromLeft (18.0f), juce::Justification::centredLeft);
            g.setColour (hub.displayColour (s));
            g.fillEllipse (rr.getX(), rr.getCentreY() - 5.0f, 10.0f, 10.0f);
            rr.removeFromLeft (18.0f);

            auto grip = rr.removeFromRight (14.0f);
            g.setColour (colours::text3);
            for (int k = 0; k < 3; ++k)
                for (int c = 0; c < 2; ++c)
                    g.fillEllipse (grip.getX() + 3.0f + 5.0f * (float) c, grip.getCentreY() - 6.0f + 5.0f * (float) k, 2.0f, 2.0f);

            rr.removeFromRight (6.0f);
            auto star = rr.removeFromRight (22.0f);
            if (s == master || hoverStar == i)
            {
                g.setColour (s == master ? colours::accent : colours::text3);
                g.setFont (font (12.0f));
                g.drawText (juce::String::fromUTF8 ("★"), star, juce::Justification::centred);
            }

            auto meterR = rr.removeFromRight (36.0f).withSizeKeepingCentre (34.0f, 3.0f);
            g.setColour (colours::line2);
            g.fillRoundedRectangle (meterR, 1.5f);
            const float m = juce::jlimit (0.0f, 1.0f, meter[(size_t) s] / 9.0f);
            g.setColour (colours::accent);
            g.fillRoundedRectangle (meterR.withLeft (meterR.getRight() - meterR.getWidth() * m), 1.5f);

            g.setColour (colours::text);
            g.setFont (font (13.0f, Weight::medium));
            g.drawText (hub.displayName (s), rr.withTrimmedRight (6.0f), juce::Justification::centredLeft, true);
        };

        for (int i = 0; i < (int) order.size(); ++i)
        {
            if (i == dragIndex && dragged)
                continue;
            int visual = i;
            if (dragged && dragIndex >= 0)
            {
                if (i > dragIndex && i <= dropIndex) visual = i - 1;
                if (i < dragIndex && i >= dropIndex) visual = i + 1;
            }
            drawRow (i, rowArea (visual), false);
        }

        if (dragged && dragIndex >= 0)
        {
            auto r = rowArea (0).withY (dragY - grabOffset).translated (4.0f, 0.0f);
            drawRow (dragIndex, r, true);
        }

        if (order.size() <= 1)
        {
            g.setColour (colours::text3);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Placez Breath sur d'autres pistes : elles apparaîtront ici automatiquement."),
                              rowArea (1).toNearestInt().withHeight (60), juce::Justification::centredLeft, 3);
        }
    }

    void HierarchyList::mouseMove (const juce::MouseEvent& e)
    {
        int h = -1;
        for (int i = 0; i < (int) order.size(); ++i)
            if (starArea (i).contains (e.position)) h = i;
        if (h != hoverStar) { hoverStar = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void HierarchyList::mouseDown (const juce::MouseEvent& e)
    {
        dragIndex = -1;
        dragged = false;
        for (int i = 0; i < (int) order.size(); ++i)
        {
            if (starArea (i).contains (e.position))
            {
                hub.setMaster (order[(size_t) i]);
                return;
            }
            if (rowArea (i).contains (e.position))
            {
                dragIndex = dropIndex = i;
                grabOffset = e.position.y - rowArea (i).getY();
                dragY = e.position.y;
            }
        }
    }

    void HierarchyList::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragIndex < 0)
            return;
        if (e.getDistanceFromDragStart() > 3)
            dragged = true;
        dragY = juce::jlimit (kListTop + grabOffset, kListTop + kRowH * (float) (order.size() - 1) + grabOffset, e.position.y);
        dropIndex = juce::jlimit (0, (int) order.size() - 1, (int) std::floor ((dragY - grabOffset - kListTop) / kRowH + 0.5f));
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        repaint();
    }

    void HierarchyList::mouseUp (const juce::MouseEvent&)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        if (dragIndex < 0)
            return;

        const int from = dragIndex, to = dropIndex;
        const bool wasDragged = dragged;
        const int slot = order[(size_t) from];
        dragIndex = -1;
        dragged = false;

        if (wasDragged && from != to)
        {
            auto newOrder = order;
            newOrder.erase (newOrder.begin() + from);
            newOrder.insert (newOrder.begin() + to, slot);
            order = newOrder;
            hub.reorder (newOrder);
        }
        else if (onSelect)
        {
            onSelect (slot);
        }
        refresh();
    }

    //==========================================================================
    RelationGrid::RelationGrid (BreathAudioProcessor& p) : proc (p), hub (p.getHub()) { refresh(); }

    void RelationGrid::refresh()
    {
        order = hub.hierarchy();
        layoutCells();
        repaint();
    }

    void RelationGrid::layoutCells()
    {
        cells.clear();
        const int n = (int) order.size();
        if (n < 2)
            return;

        const float availW = (float) getWidth() - 100.0f, availH = (float) getHeight() - 120.0f;
        cellSize = juce::jlimit (16.0f, 52.0f, juce::jmin (availW, availH) / (float) (n - 1) - 6.0f);
        origin = { 84.0f, 70.0f };

        for (int ri = 0; ri < n - 1; ++ri)
            for (int ci = 0; ci <= ri; ++ci)
                cells.push_back ({ { origin.x + (cellSize + 6.0f) * (float) ci, origin.y + (cellSize + 6.0f) * (float) ri, cellSize, cellSize },
                                   order[(size_t) ci], order[(size_t) ri + 1] });
    }

    void RelationGrid::tick()
    {
        for (auto& c : cells)
        {
            auto& v = shown[(size_t) c.upper][(size_t) c.lower];
            v += (hub.slot (c.lower).activityFrom[(size_t) c.upper].load (std::memory_order_relaxed) - v) * 0.3f;
        }
        repaint();
    }

    void RelationGrid::paint (juce::Graphics& g)
    {
        drawCaption (g, juce::String::fromUTF8 ("Relations · qui creuse qui"), { 16.0f, 8.0f, 300.0f, 16.0f });
        const int n = (int) order.size();

        if (n < 2)
        {
            g.setColour (colours::text3);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Il faut au moins deux instances de Breath pour créer une relation."),
                              getLocalBounds().reduced (24), juce::Justification::centred, 2);
            return;
        }

        const float step = cellSize + 6.0f;
        const float labelFont = cellSize >= 34.0f ? 11.0f : 9.0f;

        // En-têtes de colonnes (pistes qui creusent) et de lignes (pistes creusées)
        for (int ci = 0; ci < n - 1; ++ci)
        {
            const int s = order[(size_t) ci];
            auto r = juce::Rectangle<float> (origin.x + step * (float) ci - 3.0f, origin.y - 30.0f, step, 22.0f);
            g.setColour (hub.displayColour (s));
            g.fillEllipse (r.getCentreX() - 3.0f, r.getY(), 6.0f, 6.0f);
            g.setColour (colours::text2);
            g.setFont (font (labelFont, Weight::medium));
            g.drawText (hub.displayName (s), r.withTrimmedTop (7.0f), juce::Justification::centred, true);
        }
        for (int ri = 0; ri < n - 1; ++ri)
        {
            const int s = order[(size_t) ri + 1];
            auto r = juce::Rectangle<float> (8.0f, origin.y + step * (float) ri, origin.x - 16.0f, cellSize);
            g.setColour (s == proc.selectedLower ? colours::text : colours::text2);
            g.setFont (font (labelFont + 1.0f, Weight::medium));
            g.drawText (hub.displayName (s), r, juce::Justification::centredRight, true);

            // case « soi-même » hachurée
            auto self = juce::Rectangle<float> (origin.x + step * (float) (ri + 1), origin.y + step * (float) ri, cellSize, cellSize);
            if (ri + 1 < n - 1)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (self.toNearestInt());
                g.setColour (juce::Colour (0xff222226));
                for (float d = -cellSize; d < cellSize; d += 5.0f)
                    g.drawLine (self.getX() + d, self.getBottom(), self.getX() + d + cellSize, self.getY(), 1.0f);
            }
        }

        for (auto& c : cells)
        {
            const auto rel = hub.getRelation (c.upper, c.lower);
            const bool sel = c.upper == proc.selectedUpper && c.lower == proc.selectedLower;

            if (! rel.enabled)
            {
                const float dashes[] = { 3.0f, 3.0f };
                juce::Path p;
                p.addRoundedRectangle (c.r.reduced (0.5f), 9.0f);
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (colours::line2);
                g.fillPath (dashed);
            }
            else
            {
                const float v = shown[(size_t) c.upper][(size_t) c.lower];
                g.setColour (heat (v));
                g.fillRoundedRectangle (c.r, 9.0f);
                if (cellSize >= 30.0f)
                {
                    g.setColour (v > 1.0f ? juce::Colours::white : colours::text2);
                    g.setFont (font (10.0f, Weight::semibold));
                    g.drawText (juce::String::fromUTF8 ("−") + juce::String (v, 1), c.r, juce::Justification::centred);
                }
            }

            if (sel)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (c.r.expanded (2.0f), 10.0f, 2.0f);
            }
        }

        // Légende
        const float ly = origin.y + step * (float) (n - 1) + 14.0f;
        g.setFont (font (11.0f));
        g.setColour (colours::text3);
        g.drawText (juce::String::fromUTF8 ("Atténuation en direct"), juce::Rectangle<float> (16.0f, ly, 130.0f, 16.0f), juce::Justification::centredLeft);
        auto grad = juce::Rectangle<float> (150.0f, ly + 6.0f, 90.0f, 4.0f);
        juce::ColourGradient cg (heat (0.0f), grad.getX(), 0.0f, heat (9.0f), grad.getRight(), 0.0f, false);
        cg.addColour (0.5, heat (4.5f));
        cg.addColour (0.78, heat (7.0f));
        g.setGradientFill (cg);
        g.fillRoundedRectangle (grad, 2.0f);
        g.setColour (colours::text3);
        g.drawText (juce::String::fromUTF8 ("0 → 9 dB"), juce::Rectangle<float> (248.0f, ly, 80.0f, 16.0f), juce::Justification::centredLeft);
        g.drawText (juce::String::fromUTF8 ("Clic : régler · Double-clic : activer / désactiver"),
                    juce::Rectangle<float> (16.0f, ly + 20.0f, (float) getWidth() - 32.0f, 16.0f), juce::Justification::centredLeft);
    }

    void RelationGrid::mouseDown (const juce::MouseEvent& e)
    {
        for (auto& c : cells)
            if (c.r.expanded (3.0f).contains (e.position))
            {
                proc.selectedUpper = c.upper;
                proc.selectedLower = c.lower;
                repaint();
                if (onSelectionChange) onSelectionChange();
                return;
            }
    }

    void RelationGrid::mouseDoubleClick (const juce::MouseEvent& e)
    {
        for (auto& c : cells)
            if (c.r.expanded (3.0f).contains (e.position))
            {
                auto s = hub.getRelation (c.upper, c.lower);
                s.enabled = ! s.enabled;
                hub.beginEdit();
                hub.setRelation (c.upper, c.lower, s);
                hub.endEdit();
                return;
            }
    }

    //==========================================================================
    RelationPanel::RelationPanel (BreathAudioProcessor& p) : proc (p), hub (p.getHub())
    {
        enabled.onClick = [this] { if (! updating) { hub.beginEdit(); apply(); hub.endEdit(); } };
        addAndMakeVisible (enabled);

        auto setup = [this] (juce::Slider& s, double lo, double hi, double def, double skewMid = 0.0)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setRange (lo, hi);
            if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
            s.setDoubleClickReturnValue (true, def);
            s.onDragStart = [this] { editing = true; hub.beginEdit(); };
            s.onDragEnd   = [this] { editing = false; hub.endEdit(); };
            s.onValueChange = [this]
            {
                if (updating) return;
                if (editing) apply();
                else { hub.beginEdit(); apply(); hub.endEdit(); }
            };
            addAndMakeVisible (s);
        };

        const RelationSettings d;
        setup (amount, 0.0, 1.0, d.amount);
        setup (maxRed, 0.5, 24.0, d.maxReduction, 6.0);
        setup (threshold, -80.0, 0.0, d.threshold);
        setup (attack, 0.1, 100.0, d.attackMs, 5.0);
        setup (release, 5.0, 1000.0, d.releaseMs, 120.0);
        setup (resolution, 0.0, 1.0, d.resolution);

        range.setSliderStyle (juce::Slider::TwoValueHorizontal);
        range.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        range.setNormalisableRange ({ 20.0, 20000.0,
            [] (double lo, double hi, double n) { return lo * std::pow (hi / lo, n); },
            [] (double lo, double hi, double v) { return std::log (v / lo) / std::log (hi / lo); } });
        range.onDragStart = [this] { editing = true; hub.beginEdit(); };
        range.onDragEnd   = [this] { editing = false; hub.endEdit(); };
        range.onValueChange = [this] { if (! updating && editing) apply(); };
        addAndMakeVisible (range);

        auto hz = [] (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, 1) + "k" : juce::String (juce::roundToInt (f)); };

        rows = {
            { juce::String::fromUTF8 ("Quantité"),      &amount,     [this] { return juce::String (juce::roundToInt (amount.getValue() * 100)) + " %"; } },
            { juce::String::fromUTF8 ("Réduction max"), &maxRed,     [this] { return juce::String::fromUTF8 ("−") + juce::String (maxRed.getValue(), 1) + " dB"; } },
            { "Seuil",                                  &threshold,  [this] { return juce::String (juce::roundToInt (threshold.getValue())) + " dB"; } },
            { "Attaque",                                &attack,     [this] { return juce::String (attack.getValue(), attack.getValue() < 10 ? 1 : 0) + " ms"; } },
            { "Release",                                &release,    [this] { return juce::String (juce::roundToInt (release.getValue())) + " ms"; } },
            { "Plage",                                  &range,      [this, hz] { return hz (range.getMinValue()) + juce::String::fromUTF8 (" – ") + hz (range.getMaxValue()) + " Hz"; } },
            { juce::String::fromUTF8 ("Résolution"),    &resolution, [this]
                {
                    const double r = resolution.getValue();
                    if (r < 0.03) return juce::String ("Large bande");
                    const double sigma = 0.5 * (std::pow (2.0, (1.0 - r) * 6.0) - 1.0);
                    return juce::String::fromUTF8 ("≈ ") + juce::String (juce::jlimit (2, 64, juce::roundToInt (64.0 / (1.0 + 2.0 * sigma)))) + " bandes";
                } },
        };

        refresh();
    }

    void RelationPanel::apply()
    {
        const int u = proc.selectedUpper, l = proc.selectedLower;
        if (! hub.isActive (u) || ! hub.isActive (l))
            return;
        RelationSettings s;
        s.enabled = enabled.getToggleState();
        s.amount = (float) amount.getValue();
        s.maxReduction = (float) maxRed.getValue();
        s.threshold = (float) threshold.getValue();
        s.attackMs = (float) attack.getValue();
        s.releaseMs = (float) release.getValue();
        s.lowHz = (float) range.getMinValue();
        s.highHz = (float) range.getMaxValue();
        s.resolution = (float) resolution.getValue();
        hub.setRelation (u, l, s);
        repaint();
    }

    void RelationPanel::refresh()
    {
        ensureSelection (proc);
        const int u = proc.selectedUpper, l = proc.selectedLower;
        const bool ok = hub.isActive (u) && hub.isActive (l);

        for (auto* c : std::initializer_list<juce::Component*> { &enabled, &amount, &maxRed, &threshold, &attack, &release, &range, &resolution })
            c->setVisible (ok);

        if (ok && ! editing)
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            const auto s = hub.getRelation (u, l);
            enabled.setToggleState (s.enabled, juce::dontSendNotification);
            amount.setValue (s.amount, juce::dontSendNotification);
            maxRed.setValue (s.maxReduction, juce::dontSendNotification);
            threshold.setValue (s.threshold, juce::dontSendNotification);
            attack.setValue (s.attackMs, juce::dontSendNotification);
            release.setValue (s.releaseMs, juce::dontSendNotification);
            range.setMinAndMaxValues (s.lowHz, s.highHz, juce::dontSendNotification);
            resolution.setValue (s.resolution, juce::dontSendNotification);
            for (auto* sl : { &amount, &maxRed, &threshold, &attack, &release, &range, &resolution })
                sl->setEnabled (s.enabled);
        }
        repaint();
    }

    void RelationPanel::resized()
    {
        enabled.setBounds (getWidth() - 50, 34, 40, 26);
        float y = 76.0f;
        for (auto& r : rows)
        {
            r.slider->setBounds (juce::Rectangle<float> (16.0f, y + 20.0f, (float) getWidth() - 32.0f, 18.0f).toNearestInt().expanded (4, 0));
            y += 52.0f;
        }
    }

    void RelationPanel::paint (juce::Graphics& g)
    {
        drawCaption (g, juce::String::fromUTF8 ("Relation sélectionnée"), { 16.0f, 8.0f, 240.0f, 16.0f });
        const int u = proc.selectedUpper, l = proc.selectedLower;
        if (! hub.isActive (u) || ! hub.isActive (l))
        {
            g.setColour (colours::text3);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Cliquez sur une case de la grille pour régler une relation."),
                              getLocalBounds().reduced (20).withTrimmedTop (30), juce::Justification::centredTop, 3);
            return;
        }

        // « ● Kick → ● Basse »
        float x = 16.0f;
        const float cy = 47.0f;
        auto who = [&] (int s)
        {
            g.setColour (hub.displayColour (s));
            g.fillEllipse (x, cy - 5.0f, 10.0f, 10.0f);
            x += 16.0f;
            g.setColour (colours::text);
            g.setFont (font (14.0f, Weight::semibold));
            const auto nm = hub.displayName (s);
            const float w = juce::jmin (70.0f, font (14.0f, Weight::semibold).getStringWidthFloat (nm) + 2.0f);
            g.drawText (nm, juce::Rectangle<float> (x, cy - 10.0f, w, 20.0f), juce::Justification::centredLeft, true);
            x += w + 6.0f;
        };
        who (u);
        g.setColour (colours::text3);
        g.setFont (font (14.0f));
        g.drawText (juce::String::fromUTF8 ("→"), juce::Rectangle<float> (x, cy - 10.0f, 16.0f, 20.0f), juce::Justification::centred);
        x += 22.0f;
        who (l);

        float y = 76.0f;
        const bool en = hub.getRelation (u, l).enabled;
        for (auto& r : rows)
        {
            g.setColour (colours::line);
            g.fillRect (8.0f, y - 6.0f, (float) getWidth() - 16.0f, 1.0f);
            g.setFont (font (12.0f));
            g.setColour (en ? colours::text2 : colours::text3);
            g.drawText (r.label, juce::Rectangle<float> (16.0f, y, 140.0f, 16.0f), juce::Justification::centredLeft);
            g.setFont (font (12.0f, Weight::semibold));
            g.setColour (en ? colours::text : colours::text3);
            g.drawText (r.value(), juce::Rectangle<float> (16.0f, y, (float) getWidth() - 32.0f, 16.0f), juce::Justification::centredRight);
            y += 52.0f;
        }
    }

    //==========================================================================
    MatrixView::MatrixView (BreathAudioProcessor& p) : proc (p), list (p), grid (p), panel (p)
    {
        list.onSelect = [this] (int s)
        {
            proc.selectedLower = s;
            proc.selectedUpper = -1;
            refresh();
        };
        grid.onSelectionChange = [this] { panel.refresh(); list.repaint(); };
        addAndMakeVisible (list);
        addAndMakeVisible (grid);
        addAndMakeVisible (panel);
    }

    void MatrixView::refresh()
    {
        ensureSelection (proc);
        list.refresh();
        grid.refresh();
        panel.refresh();
    }

    void MatrixView::tick()
    {
        list.tick();
        grid.tick();
    }

    void MatrixView::resized()
    {
        auto r = getLocalBounds().toFloat();
        listCard = r.removeFromLeft (226.0f);
        r.removeFromLeft (14.0f);
        panelCard = r.removeFromRight (252.0f);
        r.removeFromRight (14.0f);
        gridCard = r;

        list.setBounds (listCard.reduced (1.0f).toNearestInt());
        grid.setBounds (gridCard.reduced (1.0f).toNearestInt());
        panel.setBounds (panelCard.reduced (1.0f).toNearestInt());
        grid.refresh();
    }

    void MatrixView::paint (juce::Graphics& g)
    {
        drawCard (g, listCard);
        drawCard (g, gridCard);
        drawCard (g, panelCard);
    }

    void MatrixView::paintOverChildren (juce::Graphics& g)
    {
        drawWarnings (g, gridCard.reduced (16.0f), proc);
    }
}
