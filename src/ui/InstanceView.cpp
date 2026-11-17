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
        : proc (p), spectrum (p),
          mixAtt (p.getParams(), "mix", mix),
          lookAtt (p.getParams(), "lookahead", lookahead),
          stereo (*p.getParams().getParameter ("stereoMode"), { juce::String::fromUTF8 ("Stéréo"), "Mid", "Side" }),
          delta (p.getParams(), "delta", juce::String::fromUTF8 ("Signal retiré")),
          bypass (p.getParams(), "bypass", "Instance")
    {
        addAndMakeVisible (spectrum);

        for (auto* s : { &mix, &lookahead })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s->onValueChange = [this] { repaint(); };
            addAndMakeVisible (*s);
        }
        mix.setDoubleClickReturnValue (true, 100.0);
        lookahead.setDoubleClickReturnValue (true, 0.0);
        lookahead.setTooltip (juce::String::fromUTF8 ("Lookahead : retarde le son de la piste pour que l'atténuation arrive à temps. "
                                                      "La latence est déclarée au DAW (compensation automatique). 0 = aucune latence."));
        stereo.fontSize = 11.0f;
        addAndMakeVisible (stereo);
        addAndMakeVisible (delta);
        addAndMakeVisible (bypass);
    }

    void InstanceView::tick()
    {
        spectrum.tick();
        const int sync = proc.getHub().getSyncDelay();
        if (sync != lastSync)
        {
            lastSync = sync;
            repaint();
        }
    }

    void InstanceView::resized()
    {
        auto r = getLocalBounds().toFloat();
        auto controls = r.removeFromBottom (96.0f);
        r.removeFromBottom (14.0f);
        spectrum.setBounds (r.toNearestInt());

        const float gap = 14.0f;
        const float unit = (controls.getWidth() - 4.0f * gap) / 5.5f;
        const float widths[] = { unit, unit, unit * 1.5f, unit, unit };
        float x = controls.getX();
        for (int i = 0; i < 5; ++i)
        {
            cards[(size_t) i] = { x, controls.getY(), widths[i], controls.getHeight() };
            x += widths[i] + gap;
        }

        auto inner = [] (juce::Rectangle<float> c) { return c.reduced (14.0f, 12.0f); };
        mix.setBounds (inner (cards[0]).removeFromBottom (18.0f).toNearestInt().expanded (6, 0));
        lookahead.setBounds (inner (cards[1]).removeFromBottom (18.0f).toNearestInt().expanded (6, 0));
        stereo.setBounds (inner (cards[2]).withTrimmedTop (26.0f).withSizeKeepingCentre (inner (cards[2]).getWidth(), 28.0f).toNearestInt());
        delta.setBounds (inner (cards[3]).withTrimmedTop (26.0f).withSizeKeepingCentre (inner (cards[3]).getWidth(), 24.0f).toNearestInt());
        bypass.setBounds (inner (cards[4]).withTrimmedTop (26.0f).withSizeKeepingCentre (inner (cards[4]).getWidth(), 24.0f).toNearestInt());
    }

    void InstanceView::paint (juce::Graphics& g)
    {
        const char* titles[] = { "Dry / Wet", "Lookahead", "", "Delta", "Bypass" };
        for (int i = 0; i < 5; ++i)
        {
            drawCard (g, cards[(size_t) i]);
            auto in = cards[(size_t) i].reduced (14.0f, 12.0f);
            drawCaption (g, i == 2 ? juce::String::fromUTF8 ("Stéréo") : juce::String (titles[i]), in.removeFromTop (14.0f));
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

        value (cards[0], juce::String (juce::roundToInt (mix.getValue())), "%");

        const double la = lookahead.getValue();
        if (la <= 0.0)
            value (cards[1], "Off", "");
        else
            value (cards[1], juce::String (la, 1), "ms");

        // Indication : délai de synchronisation entre instances.
        const double sr = juce::jmax (1.0, proc.getSampleRateSafe());
        const double syncMs = proc.getHub().getSyncDelay() * 1000.0 / sr;
        auto hint = cards[1].reduced (14.0f, 12.0f).withTrimmedTop (40.0f).removeFromTop (12.0f);
        g.setFont (font (10.0f));
        g.setColour (la + 0.05 >= syncMs ? colours::accent : colours::text3);
        g.drawText (la + 0.05 >= syncMs ? juce::String::fromUTF8 ("Réaction sans retard")
                                         : "Synchro " + juce::String (syncMs, 1) + " ms",
                    hint, juce::Justification::centredLeft);
    }

    void InstanceView::paintOverChildren (juce::Graphics& g)
    {
        auto area = spectrum.getBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedBottom (40.0f);
        drawWarnings (g, area, proc);
    }
}
