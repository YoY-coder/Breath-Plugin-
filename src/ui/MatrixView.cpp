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

        // Sélection par défaut cohérente pour la vue Matrice.
        void ensureSelection (BreathAudioProcessor& proc)
        {
            auto& hub = proc.getHub();
            const auto order = hub.hierarchy (proc.getFamily());
            const int u = proc.selectedUpper, l = proc.selectedLower;
            if (hub.isActive (u) && hub.isActive (l) && hub.familyOf (u) == proc.getFamily() && hub.familyOf (l) == proc.getFamily())
                return;

            const int me = proc.getSlot();
            const int lower = (hub.isActive (l) && hub.familyOf (l) == proc.getFamily()) ? l : (me >= 0 ? me : (order.empty() ? -1 : order.front()));
            proc.selectedLower = lower;
            proc.selectedUpper = -1;
            if (lower < 0)
                return;
            for (int sIdx : order)
                if (sIdx != lower && hub.isRelationActive (sIdx, lower)) { proc.selectedUpper = sIdx; return; }
            for (int sIdx : order)
                if (sIdx != lower) { proc.selectedUpper = sIdx; return; }
        }
    }

    //==========================================================================
    HierarchyList::HierarchyList (BreathAudioProcessor& p) : proc (p), hub (p.getHub()) { refresh(); }

    void HierarchyList::refresh()
    {
        if (dragIndex < 0)
            order = hub.hierarchy (proc.getFamily());
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
        const int master = hub.getMaster (proc.getFamily());
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
        order = hub.hierarchy (proc.getFamily());
        layoutCells();
        repaint();
    }

    void RelationGrid::layoutCells()
    {
        cells.clear();
        const int n = (int) order.size();
        if (n < 2)
            return;

        const float availW = (float) getWidth() - 96.0f, availH = (float) getHeight() - 130.0f;
        cellSize = juce::jlimit (12.0f, 46.0f, juce::jmin (availW, availH) / (float) n - 5.0f);
        origin = { 84.0f, 70.0f };
        const float step = cellSize + 5.0f;

        for (int ri = 0; ri < n; ++ri)
            for (int ci = 0; ci < n; ++ci)
                cells.push_back ({ { origin.x + step * (float) ci, origin.y + step * (float) ri, cellSize, cellSize },
                                   order[(size_t) ci], order[(size_t) ri], ri == ci });
    }

    void RelationGrid::tick()
    {
        for (auto& c : cells)
        {
            if (c.diagonal) continue;
            auto& v = shown[(size_t) c.source][(size_t) c.target];
            v += (hub.slot (c.target).activityFrom[(size_t) c.source].load (std::memory_order_relaxed) - v) * 0.3f;
        }
        repaint();
    }

    void RelationGrid::paint (juce::Graphics& g)
    {
        drawCaption (g, juce::String::fromUTF8 ("Relations · colonne = qui creuse · ligne = qui est creusé"), { 16.0f, 8.0f, (float) getWidth() - 32.0f, 16.0f });
        const int n = (int) order.size();

        if (n < 2)
        {
            g.setColour (colours::text3);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Il faut au moins deux instances de Breath pour créer une relation."),
                              getLocalBounds().reduced (24), juce::Justification::centred, 2);
            return;
        }

        const float step = cellSize + 5.0f;
        const float labelFont = cellSize >= 30.0f ? 10.0f : 8.5f;

        for (int ci = 0; ci < n; ++ci)
        {
            const int s = order[(size_t) ci];
            auto r = juce::Rectangle<float> (origin.x + step * (float) ci - 3.0f, origin.y - 30.0f, step, 22.0f);
            g.setColour (hub.displayColour (s));
            g.fillEllipse (r.getCentreX() - 3.0f, r.getY(), 6.0f, 6.0f);
            g.setColour (s == proc.selectedUpper ? colours::text : colours::text2);
            g.setFont (font (labelFont, Weight::medium));
            g.drawText (hub.displayName (s), r.withTrimmedTop (7.0f), juce::Justification::centred, true);
        }
        for (int ri = 0; ri < n; ++ri)
        {
            const int s = order[(size_t) ri];
            auto r = juce::Rectangle<float> (8.0f, origin.y + step * (float) ri, origin.x - 16.0f, cellSize);
            g.setColour (s == proc.selectedLower ? colours::text : colours::text2);
            g.setFont (font (labelFont + 1.0f, Weight::medium));
            g.drawText (hub.displayName (s), r, juce::Justification::centredRight, true);
        }

        for (auto& c : cells)
        {
            const auto rel = hub.getRelation (c.source, c.target);
            const bool above = c.diagonal || hub.isAbove (c.source, c.target);
            const bool visible = hub.isRelationVisible (c.source, c.target);
            const bool active = hub.isRelationActive (c.source, c.target);
            const bool sel = c.source == proc.selectedUpper && c.target == proc.selectedLower;
            const float radius = juce::jmin (9.0f, cellSize * 0.25f);

            if (c.diagonal && ! visible)
            {
                // Case « Self » non utilisée : hachurée (double-clic pour activer l'auto-creusement).
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (c.r.toNearestInt());
                g.setColour (juce::Colour (0xff222226));
                for (float d = -cellSize; d < cellSize; d += 5.0f)
                    g.drawLine (c.r.getX() + d, c.r.getBottom(), c.r.getX() + d + cellSize, c.r.getY(), 1.0f);
            }
            else if (visible && ! active)
            {
                // Relation en bypass : grisée.
                g.setColour (colours::panel2);
                g.fillRoundedRectangle (c.r, radius);
                if (cellSize >= 30.0f)
                {
                    g.setColour (colours::text3);
                    g.setFont (font (9.0f, Weight::semibold));
                    g.drawText ("OFF", c.r, juce::Justification::centred);
                }
            }
            else if (! active)
            {
                const float dashes[] = { 3.0f, 3.0f };
                juce::Path p;
                p.addRoundedRectangle (c.r.reduced (0.5f), radius);
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (above ? colours::line2 : colours::line);
                g.fillPath (dashed);
            }
            else
            {
                const float v = shown[(size_t) c.source][(size_t) c.target];
                g.setColour (heat (v));
                g.fillRoundedRectangle (c.r, radius);
                if (cellSize >= 30.0f)
                {
                    g.setColour (v > 1.0f ? juce::Colours::white : colours::text2);
                    g.setFont (font (10.0f, Weight::semibold));
                    g.drawText (juce::String::fromUTF8 ("−") + juce::String (v, 1), c.r, juce::Justification::centred);
                }
                if (c.diagonal && cellSize >= 20.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.setFont (font (8.5f, Weight::semibold));
                    g.drawText ("SELF", c.r.withTrimmedTop (c.r.getHeight() * 0.62f), juce::Justification::centred);
                }
                if (! rel.followOrder && ! c.diagonal)
                {
                    // Relation libre (indépendante de la hiérarchie) : petit repère.
                    g.setColour (juce::Colours::white.withAlpha (0.85f));
                    g.fillEllipse (c.r.getRight() - 8.0f, c.r.getY() + 4.0f, 4.0f, 4.0f);
                }
            }

            if (sel)
            {
                g.setColour (juce::Colours::white);
                g.drawRoundedRectangle (c.r.expanded (2.0f), radius + 1.0f, 2.0f);
            }
        }

        // Légende
        const float ly = origin.y + step * (float) n + 10.0f;
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
        g.drawFittedText (juce::String::fromUTF8 ("Clic : régler · Double-clic : ajouter / retirer. Sous la diagonale : la piste du dessus creuse celle du dessous. "
                                                  "Au-dessus : relations libres (point blanc). Diagonale : Self (la piste se creuse elle-même)."),
                          juce::Rectangle<float> (16.0f, ly + 20.0f, (float) getWidth() - 32.0f, 48.0f).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }

    void RelationGrid::mouseDown (const juce::MouseEvent& e)
    {
        for (auto& c : cells)
            if (c.r.expanded (2.0f).contains (e.position))
            {
                proc.selectedUpper = c.source;
                proc.selectedLower = c.target;
                repaint();
                if (onSelectionChange) onSelectionChange();
                return;
            }
    }

    void RelationGrid::mouseDoubleClick (const juce::MouseEvent& e)
    {
        for (auto& c : cells)
            if (c.r.expanded (2.0f).contains (e.position))
            {
                hub.toggleRelation (c.source, c.target);
                return;
            }
    }

    //==========================================================================
    RelationPanel::RelationPanel (BreathAudioProcessor& p, Mode m) : proc (p), hub (p.getHub()), mode (m)
    {
        enabled.setTooltip (juce::String::fromUTF8 ("Bypass de cette relation (elle reste en place, grisée)."));
        enabled.onClick = [this]
        {
            if (updating) return;
            auto s = hub.getRelation (source(), target());
            s.enabled = enabled.getToggleState();
            hub.beginEdit(); hub.setRelation (source(), target(), s); hub.endEdit();
        };
        addAndMakeVisible (enabled);

        auto applyToggles = [this] (bool) { if (! updating) { hub.beginEdit(); apply(); hub.endEdit(); } };
        follow.setTooltip (juce::String::fromUTF8 ("Activé : la relation ne joue que si la source est au-dessus dans la hiérarchie "
                                                   "(l'ordre et l'automation de Priorité la font basculer). Désactivé : relation libre, toujours active."));
        masking.setTooltip (juce::String::fromUTF8 ("Ne creuser que là où les deux pistes ont vraiment de l'énergie (vrais conflits)."));
        autoThr.setTooltip (juce::String::fromUTF8 ("Seuil relatif au spectre moyen de la source : indépendant du niveau d'entrée. "
                                                    "En Self, sert de suppresseur de résonances."));
        for (auto* t : { &follow, &masking, &autoThr })
        {
            t->onChange = applyToggles;
            addAndMakeVisible (*t);
        }
        hardMode.setTooltip (juce::String::fromUTF8 ("Soft : réduction douce (jusqu'à 24 dB). Hard : réduction franche, jusqu'à 70 dB."));
        hardMode.onChange = [this] (int) { if (! updating) { hub.beginEdit(); apply(); hub.endEdit(); } };
        addAndMakeVisible (hardMode);

        auto setup = [this] (juce::Slider& s, double lo, double hi, double def, double skewMid = 0.0, bool rotary = false)
        {
            s.setSliderStyle (rotary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
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
        setup (attack, 0.1, 100.0, d.attackMs, 5.0);
        setup (release, 5.0, 1000.0, d.releaseMs, 120.0);
        setup (resolution, 0.0, 1.0, d.resolution);
        setup (detailLow, -1.0, 1.0, 0.0, 0.0, true);
        setup (detailHigh, -1.0, 1.0, 0.0, 0.0, true);
        detailLow.setTooltip (juce::String::fromUTF8 ("Détail low : affine (droite) ou élargit (gauche) la résolution dans le bas du spectre."));
        detailHigh.setTooltip (juce::String::fromUTF8 ("Détail high : affine (droite) ou élargit (gauche) la résolution dans le haut du spectre."));

        rows = {
            { juce::String::fromUTF8 ("Quantité"),   &amount,     [this] { return juce::String (juce::roundToInt (amount.getValue() * 100)) + " %"; } },
            { "Attaque",                             &attack,     [this] { return juce::String (attack.getValue(), attack.getValue() < 10 ? 1 : 0) + " ms"; } },
            { "Release",                             &release,    [this] { return juce::String (juce::roundToInt (release.getValue())) + " ms"; } },
            { juce::String::fromUTF8 ("Résolution"), &resolution, [this]
                {
                    const double r = resolution.getValue();
                    if (r < 0.03) return juce::String ("Large bande");
                    const double sigma = 0.5 * (std::pow (2.0, (1.0 - r) * 6.0) - 1.0);
                    const int t = target();
                    const int nb = hub.isActive (t) && hub.info (t).client != nullptr ? hub.info (t).client->getFilterBands() : 64;
                    return juce::String::fromUTF8 ("≈ ") + juce::String (juce::jlimit (2, nb, juce::roundToInt (64.0 / (1.0 + 2.0 * sigma) * nb / 64.0))) + " bandes";
                } },
        };

        refresh();
    }

    int RelationPanel::source() const { return mode == Mode::instance ? proc.instUpper : proc.selectedUpper; }
    int RelationPanel::target() const { return mode == Mode::instance ? proc.getSlot() : proc.selectedLower; }

    void RelationPanel::apply()
    {
        const int u = source(), l = target();
        if (! hub.isActive (u) || ! hub.isActive (l))
            return;
        auto s = hub.getRelation (u, l);   // garde plage, seuil, réduction max et points (réglés sur le graphique)
        if (u != l && hub.isAbove (u, l))
            s.followOrder = follow.getState();
        s.masking = masking.getState();
        s.autoThreshold = autoThr.getState();
        s.hard = hardMode.getSelected() == 1;
        s.maxReduction = juce::jmin (s.maxReduction, s.maxLimit());
        s.amount = (float) amount.getValue();
        s.attackMs = (float) attack.getValue();
        s.releaseMs = (float) release.getValue();
        s.resolution = (float) resolution.getValue();
        s.detailLow = (float) detailLow.getValue();
        s.detailHigh = (float) detailHigh.getValue();
        hub.setRelation (u, l, s);
        repaint();
    }

    void RelationPanel::refresh()
    {
        if (mode == Mode::matrix)
            ensureSelection (proc);
        const int u = source(), l = target();
        const bool ok = hub.isActive (u) && hub.isActive (l) && (mode == Mode::matrix || hub.isRelationVisible (u, l));

        for (auto* c : std::initializer_list<juce::Component*> { &enabled, &follow, &masking, &autoThr, &hardMode,
                                                                 &amount, &attack, &release, &resolution, &detailLow, &detailHigh })
            c->setVisible (ok);

        if (ok && ! editing)
        {
            const juce::ScopedValueSetter<bool> svs (updating, true);
            const auto s = hub.getRelation (u, l);
            const bool isSelf = u == l;
            const bool above = ! isSelf && hub.isAbove (u, l);
            const bool on = s.enabled;
            enabled.setToggleState (on, juce::dontSendNotification);
            enabled.setEnabled (hub.isRelationVisible (u, l));
            follow.setState (s.followOrder);
            follow.setEnabled (above);            // relation vers le haut ou Self : toujours « libre »
            masking.setState (s.masking);
            masking.setEnabled (! isSelf && on);
            autoThr.setState (s.autoThreshold);
            autoThr.setEnabled (on);
            hardMode.setSelected (s.hard ? 1 : 0);
            hardMode.setEnabled (on);
            amount.setValue (s.amount, juce::dontSendNotification);
            attack.setValue (s.attackMs, juce::dontSendNotification);
            release.setValue (s.releaseMs, juce::dontSendNotification);
            resolution.setValue (s.resolution, juce::dontSendNotification);
            detailLow.setValue (s.detailLow, juce::dontSendNotification);
            detailHigh.setValue (s.detailHigh, juce::dontSendNotification);
            for (auto* sl : { &amount, &attack, &release, &resolution, &detailLow, &detailHigh })
                sl->setEnabled (on);
        }
        repaint();
    }

    void RelationPanel::resized()
    {
        enabled.setBounds (getWidth() - 50, 30, 40, 26);
        float y = 64.0f;
        const float rh = rowHeight();
        for (auto& r : rows)
        {
            r.slider->setBounds (juce::Rectangle<float> (16.0f, y + 16.0f, (float) getWidth() - 32.0f, 16.0f).toNearestInt().expanded (4, 0));
            y += rh;
        }
        // Potards Détail low / high
        knobsArea = { 8.0f, y, (float) getWidth() - 16.0f, 64.0f };
        const float half = knobsArea.getWidth() / 2.0f;
        detailLow.setBounds (juce::Rectangle<float> (knobsArea.getX(), knobsArea.getY(), half, 44.0f).withSizeKeepingCentre (42.0f, 42.0f).toNearestInt());
        detailHigh.setBounds (juce::Rectangle<float> (knobsArea.getX() + half, knobsArea.getY(), half, 44.0f).withSizeKeepingCentre (42.0f, 42.0f).toNearestInt());
        y += 68.0f;

        const float w = (float) getWidth() - 32.0f;
        hardMode.setBounds (juce::Rectangle<float> (16.0f, y, w, 24.0f).toNearestInt());
        y += 30.0f;
        masking.setBounds (juce::Rectangle<float> (16.0f, y, w / 2.0f - 6.0f, 24.0f).toNearestInt());
        autoThr.setBounds (juce::Rectangle<float> (16.0f + w / 2.0f + 6.0f, y, w / 2.0f - 6.0f, 24.0f).toNearestInt());
        y += 28.0f;
        follow.setBounds (juce::Rectangle<float> (16.0f, y, w, 24.0f).toNearestInt());
    }

    void RelationPanel::paint (juce::Graphics& g)
    {
        drawCaption (g, mode == Mode::instance ? juce::String::fromUTF8 ("Relation") : juce::String::fromUTF8 ("Relation sélectionnée"),
                     { 16.0f, 8.0f, 240.0f, 16.0f });
        const int u = source(), l = target();
        const bool ok = hub.isActive (u) && hub.isActive (l) && (mode == Mode::matrix || hub.isRelationVisible (u, l));
        if (! ok)
        {
            g.setColour (colours::text3);
            g.setFont (font (12.0f));
            g.drawFittedText (mode == Mode::instance
                                  ? juce::String::fromUTF8 ("Aucune piste ne creuse celle-ci. Utilisez « + » au-dessus du spectre pour en ajouter une, "
                                                            "ou « Self » pour que la piste se creuse elle-même (compresseur / anti-résonances).")
                                  : juce::String::fromUTF8 ("Cliquez sur une case de la grille pour régler une relation."),
                              getLocalBounds().reduced (20).withTrimmedTop (30), juce::Justification::centredTop, 6);
            return;
        }

        // « ● Kick → ● Basse »  ou  « ● Basse · Self »
        float x = 16.0f;
        const float cy = 43.0f;
        auto who = [&] (int sl, const juce::String& nameOverride = {})
        {
            g.setColour (hub.displayColour (sl));
            g.fillEllipse (x, cy - 5.0f, 10.0f, 10.0f);
            x += 16.0f;
            g.setColour (colours::text);
            g.setFont (font (14.0f, Weight::semibold));
            const auto nm = nameOverride.isNotEmpty() ? nameOverride : hub.displayName (sl);
            const float w = juce::jmin (68.0f, textWidth (font (14.0f, Weight::semibold), nm) + 2.0f);
            g.drawText (nm, juce::Rectangle<float> (x, cy - 10.0f, w, 20.0f), juce::Justification::centredLeft, true);
            x += w + 6.0f;
        };
        if (u == l)
        {
            who (u, hub.displayName (u) + " · Self");
        }
        else
        {
            who (u);
            g.setColour (colours::text3);
            g.setFont (font (14.0f));
            g.drawText (juce::String::fromUTF8 ("→"), juce::Rectangle<float> (x, cy - 10.0f, 16.0f, 20.0f), juce::Justification::centred);
            x += 22.0f;
            who (l);
        }

        float y = 64.0f;
        const float rh = rowHeight();
        const bool en = hub.getRelation (u, l).enabled;
        for (auto& r : rows)
        {
            g.setColour (colours::line);
            g.fillRect (8.0f, y - 4.0f, (float) getWidth() - 16.0f, 1.0f);
            g.setFont (font (12.0f));
            g.setColour (en ? colours::text2 : colours::text3);
            g.drawText (r.label, juce::Rectangle<float> (16.0f, y, 140.0f, 15.0f), juce::Justification::centredLeft);
            g.setFont (font (12.0f, Weight::semibold));
            g.setColour (en ? colours::text : colours::text3);
            g.drawText (r.value(), juce::Rectangle<float> (16.0f, y, (float) getWidth() - 32.0f, 15.0f), juce::Justification::centredRight);
            y += rh;
        }

        // Libellés des potards
        g.setColour (colours::line);
        g.fillRect (8.0f, knobsArea.getY() - 4.0f, (float) getWidth() - 16.0f, 1.0f);
        const float half = knobsArea.getWidth() / 2.0f;
        auto knobLabel = [&] (float x0, const juce::String& t, double v)
        {
            g.setFont (font (10.0f, Weight::medium));
            g.setColour (en ? colours::text2 : colours::text3);
            const juce::String val = (v > 0.005 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + " %";
            g.drawText (t + "  " + val, juce::Rectangle<float> (x0, knobsArea.getY() + 46.0f, half, 14.0f), juce::Justification::centred);
        };
        knobLabel (knobsArea.getX(), juce::String::fromUTF8 ("Détail low"), detailLow.getValue());
        knobLabel (knobsArea.getX() + half, juce::String::fromUTF8 ("Détail high"), detailHigh.getValue());
        g.setColour (colours::line);
        g.fillRect (8.0f, knobsArea.getBottom() + 1.0f, (float) getWidth() - 16.0f, 1.0f);
    }

    //==========================================================================
    MatrixView::MatrixView (BreathAudioProcessor& p) : proc (p), list (p), grid (p), panel (p, RelationPanel::Mode::matrix)
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
        if (proc.isDisplayBypassed())
        {
            g.setColour (colours::warning.withAlpha (0.9f));
            g.setFont (font (11.0f, Weight::semibold));
            g.drawText (proc.getHub().getGlobalBypassAll (proc.getFamily()) ? "BYPASS ALL" : "BYPASS (cette instance)",
                        gridCard.reduced (16.0f, 8.0f).removeFromTop (16.0f), juce::Justification::centredRight);
        }
        drawWarnings (g, gridCard.reduced (16.0f), proc);
    }
}
