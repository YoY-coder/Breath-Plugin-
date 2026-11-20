#include "TopBar.h"

namespace breath::ui
{
    TopBar::TopBar (BreathAudioProcessor& p) : proc (p), hub (p.getHub())
    {
        name.setEditable (false, true, false);
        name.setFont (font (14.0f, Weight::semibold));
        name.setTooltip (juce::String::fromUTF8 ("Double-cliquez pour renommer. Laissez vide pour reprendre le nom de la piste du DAW."));
        name.onTextChange = [this]
        {
            hub.setManualName (proc.getSlot(), name.getText());
            refresh();
        };
        addAndMakeVisible (name);

        views.onChange = [this] (int v) { if (onViewChange) onViewChange (v); };
        addAndMakeVisible (views);

        undoBtn.setTooltip ("Annuler");
        redoBtn.setTooltip (juce::String::fromUTF8 ("Rétablir"));
        undoBtn.onClick = [this] { hub.undo(); };
        redoBtn.onClick = [this] { hub.redo(); };
        addAndMakeVisible (undoBtn);
        addAndMakeVisible (redoBtn);

        snapshots.setTooltip (juce::String::fromUTF8 ("Snapshots A / B : vos modifications sont enregistrées dans le snapshot actif. "
                                                      "Cliquez sur l'autre pour comparer. « A→B » copie A dans B."));
        snapshots.onSelect = [this] (int i) { hub.selectSnapshot (i); };
        snapshots.onCopy = [this] { hub.copySnapshot (0, 1); };
        addAndMakeVisible (snapshots);

        macro.setSliderStyle (juce::Slider::LinearHorizontal);
        macro.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        macro.setRange (0.0, 1.0);
        macro.setDoubleClickReturnValue (true, 1.0);
        macro.setTooltip (juce::String::fromUTF8 ("Macro Breath : dose toutes les relations. Portée par l'instance maîtresse (★) : "
                                                  "c'est sur sa piste qu'on l'automatise."));
        macro.onValueChange = [this] { hub.setGlobalMacro (proc.getFamily(), (float) macro.getValue()); repaint(); };
        addAndMakeVisible (macro);

        bypassAll.setClickingTogglesState (false);
        bypassAll.setTooltip (juce::String::fromUTF8 ("Coupe le traitement sur toutes les instances de cette famille."));
        bypassAll.onClick = [this] { hub.setGlobalBypassAll (proc.getFamily(), ! hub.getGlobalBypassAll (proc.getFamily())); tick(); };

        familyBtn.setTooltip (juce::String::fromUTF8 ("Famille de cette instance : seules les instances d'une même famille se voient "
                                                      "et peuvent se creuser."));
        familyBtn.onClick = [this]
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Famille");
            for (int f = 0; f < kNumFamilies; ++f)
                m.addItem (f + 1, BreathHub::familyGlyph (f) + "  " + BreathHub::familyName (f)
                                      + "  (" + juce::String (hub.numActive (f)) + ")", true, f == proc.getFamily());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&familyBtn), [this] (int r)
            {
                if (r <= 0) return;
                auto* p = proc.getParams().getParameter ("family");
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 1)));
                p->endChangeGesture();
                hub.setFamily (proc.getSlot(), r - 1);
            });
        };
        addAndMakeVisible (familyBtn);
        addAndMakeVisible (bypassAll);

        refresh();
        tick();
    }

    void TopBar::setView (int v)
    {
        views.setSelected (v);
        undoBtn.setVisible (true);
        redoBtn.setVisible (true);
    }

    void TopBar::refresh()
    {
        const int s = proc.getSlot();
        if (s < 0)
        {
            name.setText ("Breath", juce::dontSendNotification);
            return;
        }

        if (! name.isBeingEdited())
            name.setText (hub.displayName (s), juce::dontSendNotification);
        dot = hub.displayColour (s);

        const auto order = hub.hierarchy (proc.getFamily());
        familyBtn.family = proc.getFamily();
        familyBtn.repaint();
        rankShown = (int) (std::find (order.begin(), order.end(), s) - order.begin()) + 1;

        undoBtn.setEnabled (hub.canUndo());
        redoBtn.setEnabled (hub.canRedo());
        snapshots.active = hub.getActiveSnapshot();
        snapshots.repaint();
        repaint();
    }

    void TopBar::tick()
    {
        if (proc.getSlot() < 0)
            return;

        const float m = hub.getGlobalMacro (proc.getFamily());
        if (! macro.isMouseButtonDown() && std::abs (m - macroShown) > 1.0e-4f)
        {
            macroShown = m;
            macro.setValue (m, juce::dontSendNotification);
            repaint();
        }

        const bool b = hub.getGlobalBypassAll (proc.getFamily());
        if (bypassAll.getToggleState() != b)
            bypassAll.setToggleState (b, juce::dontSendNotification);

        undoBtn.setEnabled (hub.canUndo());
        redoBtn.setEnabled (hub.canRedo());
    }

    void TopBar::resized()
    {
        auto r = getLocalBounds().reduced (14, 0);
        familyBtn.setBounds (r.removeFromLeft (26).withSizeKeepingCentre (26, 26));
        r.removeFromLeft (10);
        dotX = (float) r.getX();
        auto left = r.removeFromLeft (170);
        left.removeFromLeft (16);
        name.setBounds (left.withSizeKeepingCentre (left.getWidth() - 34, 26).withX (left.getX() - 4));

        views.setBounds (r.removeFromLeft (156).withSizeKeepingCentre (156, 28));

        auto right = r;
        bypassAll.setBounds (right.removeFromRight (74).withSizeKeepingCentre (74, 26));
        right.removeFromRight (14);
        right.removeFromRight (36);               // valeur %
        macro.setBounds (right.removeFromRight (110).withSizeKeepingCentre (110, 20));
        right.removeFromRight (48);               // « Breath »
        right.removeFromRight (10);
        snapshots.setBounds (right.removeFromRight (94).withSizeKeepingCentre (94, 28));
        right.removeFromRight (12);
        redoBtn.setBounds (right.removeFromRight (26).withSizeKeepingCentre (26, 26));
        undoBtn.setBounds (right.removeFromRight (26).withSizeKeepingCentre (26, 26));
    }

    void TopBar::paint (juce::Graphics& g)
    {
        g.setColour (colours::line);
        g.fillRect (0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);

        // Fine ligne à la couleur de la famille sous la barre (subtil).
        g.setColour (BreathHub::familyColour (proc.getFamily()).withAlpha (0.35f));
        g.fillRect (0.0f, (float) getHeight() - 1.0f, (float) getWidth(), 1.0f);

        g.setColour (dot);
        g.fillEllipse (dotX, (float) getHeight() / 2.0f - 5.0f, 10.0f, 10.0f);

        // Rang (position dans la hiérarchie)
        if (rankShown > 0)
        {
            const auto txtW = textWidth (font (14.0f, Weight::semibold), name.getText()) + 8.0f;
            auto badge = juce::Rectangle<float> (name.getX() + juce::jmin (txtW, (float) name.getWidth()) + 6.0f,
                                                 (float) getHeight() / 2.0f - 9.0f, 22.0f, 18.0f);
            g.setColour (colours::panel2);
            g.fillRoundedRectangle (badge, 6.0f);
            g.setColour (colours::text2);
            g.setFont (font (11.0f, Weight::medium));
            g.drawText (juce::String (rankShown), badge, juce::Justification::centred);
        }

        // « Breath  72% »
        g.setFont (font (12.0f));
        g.setColour (colours::text2);
        g.drawText ("Breath", juce::Rectangle<int> (macro.getX() - 52, 0, 48, getHeight()), juce::Justification::centredRight);
        g.setColour (colours::text);
        g.setFont (font (12.0f, Weight::medium));
        g.drawText (juce::String (juce::roundToInt (macro.getValue() * 100.0)) + "%",
                    juce::Rectangle<int> (macro.getRight() + 6, 0, 40, getHeight()), juce::Justification::centredLeft);
    }
}
