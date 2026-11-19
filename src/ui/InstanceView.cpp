#include "InstanceView.h"

namespace breath::ui
{
    void drawWarnings (juce::Graphics& g, juce::Rectangle<float> area, const BreathAudioProcessor& proc)
    {
        juce::StringArray msgs;
        if (proc.isIsolated())
            msgs.add (juce::String::fromUTF8 ("Aucune autre instance Breath détectée : votre DAW isole peut-être les plugins."));
        if (proc.hasOfflineDesync())
            msgs.add (juce::String::fromUTF8 ("Export : certaines pistes n'ont pas pu être synchronisées."));
        if (msgs.isEmpty())
            return;

        g.setFont (font (11.0f, Weight::medium));
        float y = area.getBottom();
        for (auto& m : msgs)
        {
            const float w = textWidth (font (11.0f, Weight::medium), m) + 30.0f;
            auto r = juce::Rectangle<float> (area.getX(), y - 24.0f, w, 22.0f);
            g.setColour (colours::warning.withAlpha (0.14f));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (colours::warning);
            g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f);
            g.setColour (colours::text);
            g.drawText (m, r.withTrimmedLeft (21.0f), juce::Justification::centredLeft);
            y -= 28.0f;
        }
    }

    InstanceView::InstanceView (BreathAudioProcessor& p)
        : proc (p), spectrum (p), panel (p, RelationPanel::Mode::instance),
          mixAtt (p.getParams(), "mix", mix),
          makeupAtt (p.getParams(), "makeup", makeup),
          lookAtt (p.getParams(), "lookahead", lookahead),
          autoMakeup (p.getParams(), "autoMakeup", "Auto"),
          stereo (*p.getParams().getParameter ("stereoMode"), { juce::String::fromUTF8 ("Stéréo"), "Mid", "Side" }),
          delta (p.getParams(), "delta", juce::String::fromUTF8 ("Delta")),
          bypass (p.getParams(), "bypass", "Bypass")
    {
        addAndMakeVisible (spectrum);
        addAndMakeVisible (panel);
        spectrum.onSelectionChange = [this] { panel.refresh(); };

        for (auto* s : { &mix, &makeup, &lookahead })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->onValueChange = [this] { repaint(); };
            addAndMakeVisible (*s);
        }
        mix.setDoubleClickReturnValue (true, 100.0);
        makeup.setDoubleClickReturnValue (true, 0.0);
        lookahead.setDoubleClickReturnValue (true, 0.0);
        makeup.setTooltip (juce::String::fromUTF8 ("Make-up : compense le volume perdu par la réduction. "
                                                   "« Auto » ajoute en plus la compensation mesurée en continu."));
        lookahead.setTooltip (juce::String::fromUTF8 ("Lookahead : retarde le son de la piste pour que l'atténuation arrive à temps. "
                                                      "La latence est déclarée au DAW (compensation automatique). 0 = aucune latence."));
        delta.setTooltip (juce::String::fromUTF8 ("Écouter uniquement ce que Breath retire."));
        stereo.fontSize = 11.0f;
        addAndMakeVisible (autoMakeup);
        addAndMakeVisible (stereo);
        addAndMakeVisible (delta);
        addAndMakeVisible (bypass);
    }

    void InstanceView::tick()
    {
        spectrum.tick();
        const int sync = proc.getHub().getSyncDelay();
        const float a = proc.getAutoMakeupDb();
        if (sync != lastSync || std::abs (a - lastAuto) > 0.05f)
        {
            lastSync = sync;
            lastAuto = a;
            repaint (cards[1].getSmallestIntegerContainer());
            repaint (cards[2].getSmallestIntegerContainer());
        }
    }

    void InstanceView::resized()
    {
        auto r = getLocalBounds().toFloat();
        auto controls = r.removeFromBottom (96.0f);
        r.removeFromBottom (14.0f);
        panelCard = r.removeFromRight (252.0f);
        r.removeFromRight (14.0f);
        spectrum.setBounds (r.toNearestInt());
        panel.setBounds (panelCard.reduced (1.0f).toNearestInt());

        const float gap = 14.0f;
        const float unit = (controls.getWidth() - 4.0f * gap) / 5.3f;
        const float widths[] = { unit, unit * 1.15f, unit, unit * 1.15f, unit };
        float x = controls.getX();
        for (int i = 0; i < 5; ++i)
        {
            cards[(size_t) i] = { x, controls.getY(), widths[i], controls.getHeight() };
            x += widths[i] + gap;
        }

        auto inner = [] (juce::Rectangle<float> c) { return c.reduced (14.0f, 12.0f); };
        mix.setBounds (inner (cards[0]).removeFromBottom (18.0f).toNearestInt().expanded (6, 0));
        makeup.setBounds (inner (cards[1]).removeFromBottom (18.0f).toNearestInt().expanded (6, 0));
        autoMakeup.setBounds (inner (cards[1]).removeFromTop (16.0f).removeFromRight (78.0f).toNearestInt().expanded (0, 4));
        lookahead.setBounds (inner (cards[2]).removeFromBottom (18.0f).toNearestInt().expanded (6, 0));
        stereo.setBounds (inner (cards[3]).withTrimmedTop (26.0f).withSizeKeepingCentre (inner (cards[3]).getWidth(), 28.0f).toNearestInt());
        auto ec = inner (cards[4]).withTrimmedTop (18.0f);
        delta.setBounds (ec.removeFromTop (26.0f).toNearestInt());
        bypass.setBounds (ec.removeFromTop (26.0f).toNearestInt());
    }

    void InstanceView::paint (juce::Graphics& g)
    {
        drawCard (g, panelCard);

        const char* titles[] = { "Dry / Wet", "Make-up", "Lookahead", "", "" };
        for (int i = 0; i < 5; ++i)
        {
            drawCard (g, cards[(size_t) i]);
            auto in = cards[(size_t) i].reduced (14.0f, 12.0f);
            const juce::String t = i == 3 ? juce::String::fromUTF8 ("Stéréo") : (i == 4 ? juce::String::fromUTF8 ("Écoute") : juce::String (titles[i]));
            drawCaption (g, t, in.removeFromTop (14.0f));
        }

        auto value = [&] (juce::Rectangle<float> card, const juce::String& v, const juce::String& unit)
        {
            auto in = card.reduced (14.0f, 12.0f).withTrimmedTop (16.0f).removeFromTop (24.0f);
            g.setColour (colours::text);
            g.setFont (font (18.0f, Weight::semibold));
            g.drawText (v, in, juce::Justification::centredLeft);
            const float w = textWidth (font (18.0f, Weight::semibold), v);
            g.setColour (colours::text2);
            g.setFont (font (11.0f, Weight::medium));
            g.drawText (unit, in.withTrimmedLeft (w + 3.0f).withTrimmedTop (4.0f), juce::Justification::centredLeft);
        };

        auto hintText = [&] (juce::Rectangle<float> card, const juce::String& t, juce::Colour c)
        {
            auto hint = card.reduced (14.0f, 12.0f).withTrimmedTop (40.0f).removeFromTop (12.0f);
            g.setFont (font (10.0f));
            g.setColour (c);
            g.drawText (t, hint, juce::Justification::centredLeft);
        };

        value (cards[0], juce::String (juce::roundToInt (mix.getValue())), "%");

        const double mk = makeup.getValue();
        value (cards[1], (mk > 0.05 ? "+" : "") + juce::String (mk, 1), "dB");
        if (proc.getParams().getRawParameterValue ("autoMakeup")->load() > 0.5f)
            hintText (cards[1], juce::String::fromUTF8 ("Auto : +") + juce::String (proc.getAutoMakeupDb(), 1) + " dB", colours::accent);

        const double la = lookahead.getValue();
        if (la <= 0.0) value (cards[2], "Off", "");
        else           value (cards[2], juce::String (la, 1), "ms");

        // Indication : délai de synchronisation entre instances.
        const double sr = juce::jmax (1.0, proc.getSampleRateSafe());
        const double syncMs = proc.getHub().getSyncDelay() * 1000.0 / sr;
        const bool ok = la + 0.05 >= syncMs;
        hintText (cards[2], ok ? juce::String::fromUTF8 ("Réaction sans retard") : "Synchro " + juce::String (syncMs, 1) + " ms",
                  ok ? colours::accent : colours::text3);
    }

    void InstanceView::paintOverChildren (juce::Graphics& g)
    {
        auto area = spectrum.getBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedBottom (56.0f);
        drawWarnings (g, area, proc);
    }
}
