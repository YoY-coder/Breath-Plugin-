#include "EnsembleView.h"

namespace breath::ui
{
    namespace
    {
        constexpr float kHeaderH = 34.0f;

        bool readLive (const Slot& s, FrameData& out)
        {
            const auto q = s.live.seq.load (std::memory_order_acquire);
            return q >= 0 && s.live.read (q, INT64_MIN, out);
        }
    }

    EnsembleView::EnsembleView (BreathAudioProcessor& p)
        : proc (p), hub (p.getHub()), spectrum (p), panel (p, RelationPanel::Mode::external)
    {
        for (auto& a : own) a.fill (kSilenceDb);
        addChildComponent (spectrum);
        addChildComponent (panel);
        panel.setExternal (&focusSlot, &focusSource);
        spectrum.onSelectionChange = [this] { panel.refresh(); };
        refresh();
    }

    void EnsembleView::refresh()
    {
        order = hub.hierarchy (proc.getFamily());
        if (focusSlot >= 0 && std::find (order.begin(), order.end(), focusSlot) == order.end())
            focus (-1);
        if (focusSlot >= 0)
        {
            spectrum.refresh();
            panel.refresh();
        }
        layoutTiles();
        repaint();
    }

    void EnsembleView::focus (int slot)
    {
        focusSlot = slot;
        const bool on = slot >= 0;
        if (on)
        {
            focusSource = -1;
            spectrum.setTarget (slot, &focusSource);
            panel.refresh();
        }
        spectrum.setVisible (on);
        panel.setVisible (on);
        resized();
        repaint();
    }

    void EnsembleView::layoutTiles()
    {
        tiles.clear();
        const int n = (int) order.size();
        if (n == 0 || focusSlot >= 0)
            return;

        const int cols = n <= 1 ? 1 : (n <= 4 ? 2 : (n <= 9 ? 3 : 4));
        const int rows = (n + cols - 1) / cols;
        auto area = getLocalBounds().toFloat().withTrimmedTop (kHeaderH);
        const float gap = 12.0f;
        const float w = (area.getWidth() - gap * (float) (cols - 1)) / (float) cols;
        const float h = (area.getHeight() - gap * (float) (rows - 1)) / (float) rows;
        for (int i = 0; i < n; ++i)
            tiles.push_back ({ { area.getX() + (float) (i % cols) * (w + gap), area.getY() + (float) (i / cols) * (h + gap), w, h },
                               order[(size_t) i] });
    }

    void EnsembleView::resized()
    {
        backArea = { 0.0f, 0.0f, 150.0f, 26.0f };
        prevArea = { (float) getWidth() - 60.0f, 0.0f, 26.0f, 26.0f };
        nextArea = { (float) getWidth() - 28.0f, 0.0f, 26.0f, 26.0f };

        if (focusSlot >= 0)
        {
            auto r = getLocalBounds().toFloat().withTrimmedTop (kHeaderH);
            panelCard = r.removeFromRight (252.0f);
            r.removeFromRight (14.0f);
            spectrum.setBounds (r.toNearestInt());
            panel.setBounds (panelCard.reduced (1.0f).toNearestInt());
        }
        layoutTiles();
    }

    void EnsembleView::tick()
    {
        if (focusSlot >= 0)
        {
            spectrum.tick();
            panel.tick();
            repaint (0, 0, getWidth(), (int) kHeaderH);
            return;
        }

        FrameData f;
        for (int s : order)
        {
            auto& o = own[(size_t) s];
            if (readLive (hub.slot (s), f))
                for (int b = 0; b < kNumBands; ++b)
                    o[(size_t) b] += (f.bands[b] - o[(size_t) b]) * (f.bands[b] > o[(size_t) b] ? 0.6f : 0.18f);
            auto& rd = red[(size_t) s];
            for (int b = 0; b < kNumBands; ++b)
                rd[(size_t) b] += (hub.slot (s).reductionCurve[(size_t) b].load (std::memory_order_relaxed) - rd[(size_t) b]) * 0.5f;
        }
        repaint();
    }

    void EnsembleView::paint (juce::Graphics& g)
    {
        // En-tête
        if (focusSlot >= 0)
        {
            g.setColour (colours::panel2);
            g.fillRoundedRectangle (backArea, 7.0f);
            g.setColour (colours::text2);
            g.setFont (font (12.0f, Weight::medium));
            g.drawText (juce::String::fromUTF8 ("‹  Toutes les instances"), backArea, juce::Justification::centred);

            g.setColour (hub.displayColour (focusSlot));
            g.fillEllipse (backArea.getRight() + 16.0f, 8.0f, 10.0f, 10.0f);
            g.setColour (colours::text);
            g.setFont (font (14.0f, Weight::semibold));
            g.drawText (hub.displayName (focusSlot), juce::Rectangle<float> (backArea.getRight() + 32.0f, 0.0f, 300.0f, 26.0f),
                        juce::Justification::centredLeft);

            for (auto [r, t] : { std::pair<juce::Rectangle<float>, const char*> { prevArea, "‹" }, { nextArea, "›" } })
            {
                g.setColour (colours::panel2);
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (colours::text2);
                g.setFont (font (15.0f, Weight::semibold));
                g.drawText (juce::String::fromUTF8 (t), r.translated (0.0f, -1.0f), juce::Justification::centred);
            }
            drawCard (g, panelCard);
            return;
        }

        drawCaption (g, juce::String::fromUTF8 ("Toutes les instances de la famille · clic = ouvrir et régler"),
                     { 4.0f, 4.0f, (float) getWidth() - 8.0f, 18.0f });

        if (order.empty())
            return;

        for (auto& [r, s] : tiles)
        {
            drawCard (g, r);
            if (hoverTile == s)
            {
                g.setColour (colours::accent.withAlpha (0.5f));
                g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.5f);
            }

            // En-tête de la tuile : pastille, nom, rang, réduction actuelle
            auto head = r.reduced (12.0f, 8.0f).removeFromTop (18.0f);
            g.setColour (hub.displayColour (s));
            g.fillEllipse (head.getX(), head.getCentreY() - 4.0f, 8.0f, 8.0f);
            g.setColour (s == proc.getSlot() ? colours::text : colours::text2);
            g.setFont (font (12.0f, Weight::semibold));
            const int rank = (int) (std::find (order.begin(), order.end(), s) - order.begin()) + 1;
            g.drawText (juce::String (rank) + "  " + hub.displayName (s) + (s == proc.getSlot() ? juce::String::fromUTF8 ("  (cette piste)") : juce::String()),
                        head.withTrimmedLeft (14.0f), juce::Justification::centredLeft, true);
            float mx = 0.0f;
            for (auto v : red[(size_t) s]) mx = juce::jmax (mx, v);
            g.setColour (colours::accent);
            g.setFont (font (12.0f, Weight::semibold));
            g.drawText (juce::String::fromUTF8 ("−") + juce::String (mx, 1) + " dB", head, juce::Justification::centredRight);

            // Mini graphe : spectre de la piste + courbe de réduction
            auto gr = r.reduced (10.0f).withTrimmedTop (22.0f);
            auto xOf = [&] (int b) { return gr.getX() + freqToNorm (bandCentre (b)) * gr.getWidth(); };
            auto ySpec = [&] (float db) { return gr.getY() + juce::jlimit (0.0f, 1.0f, -db / 84.0f) * gr.getHeight(); };
            auto yRed = [&] (float db) { return gr.getY() + 2.0f + juce::jlimit (0.0f, 1.0f, db / 15.0f) * gr.getHeight() * 0.45f; };

            juce::Path sp, rp;
            for (int b = 0; b < kNumBands; b += 2)
            {
                const float x = xOf (b);
                if (b == 0) { sp.startNewSubPath (gr.getX(), ySpec (own[(size_t) s][0])); rp.startNewSubPath (gr.getX(), yRed (red[(size_t) s][0])); }
                sp.lineTo (x, ySpec (own[(size_t) s][(size_t) b]));
                rp.lineTo (x, yRed (red[(size_t) s][(size_t) b]));
            }
            sp.lineTo (gr.getRight(), ySpec (own[(size_t) s][kNumBands - 1]));
            rp.lineTo (gr.getRight(), yRed (red[(size_t) s][kNumBands - 1]));
            auto fill = sp;
            fill.lineTo (gr.getRight(), gr.getBottom());
            fill.lineTo (gr.getX(), gr.getBottom());
            fill.closeSubPath();
            g.setColour (hub.displayColour (s).withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (hub.displayColour (s).withAlpha (0.7f));
            g.strokePath (sp, juce::PathStrokeType (1.2f));
            auto rfill = rp;
            rfill.lineTo (gr.getRight(), gr.getY() + 2.0f);
            rfill.lineTo (gr.getX(), gr.getY() + 2.0f);
            rfill.closeSubPath();
            g.setColour (colours::accent.withAlpha (0.18f));
            g.fillPath (rfill);
            g.setColour (colours::accent);
            g.strokePath (rp, juce::PathStrokeType (1.5f));
        }
    }

    void EnsembleView::mouseMove (const juce::MouseEvent& e)
    {
        int h = -1;
        for (auto& [r, s] : tiles)
            if (r.contains (e.position)) h = s;
        if (h != hoverTile) { hoverTile = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void EnsembleView::mouseDown (const juce::MouseEvent& e)
    {
        if (focusSlot >= 0)
        {
            if (backArea.contains (e.position)) { focus (-1); return; }
            const auto it = std::find (order.begin(), order.end(), focusSlot);
            if (it == order.end() || order.empty()) return;
            const int i = (int) (it - order.begin()), n = (int) order.size();
            if (prevArea.contains (e.position)) focus (order[(size_t) ((i + n - 1) % n)]);
            if (nextArea.contains (e.position)) focus (order[(size_t) ((i + 1) % n)]);
            return;
        }
        for (auto& [r, s] : tiles)
            if (r.contains (e.position)) { focus (s); return; }
    }
}
