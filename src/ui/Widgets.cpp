#include "Widgets.h"

namespace breath::ui
{
    Segmented::Segmented (juce::StringArray i) : items (std::move (i)) {}

    void Segmented::setSelected (int index, bool notify)
    {
        index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);
        if (index == selected)
            return;
        selected = index;
        repaint();
        if (notify && onChange)
            onChange (selected);
    }

    void Segmented::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel2);
        g.fillRoundedRectangle (r, 8.0f);

        const float w = (r.getWidth() - 4.0f) / (float) juce::jmax (1, items.size());
        for (int i = 0; i < items.size(); ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + 2.0f + w * (float) i, r.getY() + 2.0f, w, r.getHeight() - 4.0f);
            if (i == selected)
            {
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.fillRoundedRectangle (seg.translated (0.0f, 1.0f), 6.0f);
                g.setColour (colours::raised);
                g.fillRoundedRectangle (seg, 6.0f);
            }
            g.setColour (i == selected ? colours::text : colours::text2);
            g.setFont (font (fontSize, Weight::medium));
            g.drawText (items[i], seg, juce::Justification::centred, false);
        }
    }

    void Segmented::mouseDown (const juce::MouseEvent& e)
    {
        const float w = ((float) getWidth() - 4.0f) / (float) juce::jmax (1, items.size());
        setSelected ((int) ((e.position.x - 2.0f) / w), true);
    }

    //==========================================================================
    ParamSegmented::ParamSegmented (juce::RangedAudioParameter& p, juce::StringArray i)
        : Segmented (std::move (i)),
          attachment (p, [this] (float v) { setSelected (juce::roundToInt (v)); })
    {
        onChange = [this] (int idx) { attachment.setValueAsCompleteGesture ((float) idx); };
        attachment.sendInitialUpdate();
    }

    //==========================================================================
    void IconButton::paint (juce::Graphics& g)
    {
        const bool on = isEnabled();
        g.setColour (on ? (isMouseOver() ? colours::text : colours::text2) : colours::text3.withAlpha (0.6f));

        juce::Path p;
        const auto c = getLocalBounds().toFloat().getCentre();
        const float s = 1.0f;
        if (icon == Icon::undo)
        {
            p.startNewSubPath (c.x - 3 * s, c.y - 5 * s); p.lineTo (c.x - 6 * s, c.y - 2 * s); p.lineTo (c.x - 3 * s, c.y + 1 * s);
            juce::Path arc;
            arc.startNewSubPath (c.x - 6 * s, c.y - 2 * s);
            arc.lineTo (c.x + 2 * s, c.y - 2 * s);
            arc.quadraticTo (c.x + 6 * s, c.y - 2 * s, c.x + 6 * s, c.y + 2 * s);
            arc.quadraticTo (c.x + 6 * s, c.y + 6 * s, c.x + 2 * s, c.y + 6 * s);
            arc.lineTo (c.x - 1 * s, c.y + 6 * s);
            p.addPath (arc);
        }
        else
        {
            p.startNewSubPath (c.x + 3 * s, c.y - 5 * s); p.lineTo (c.x + 6 * s, c.y - 2 * s); p.lineTo (c.x + 3 * s, c.y + 1 * s);
            juce::Path arc;
            arc.startNewSubPath (c.x + 6 * s, c.y - 2 * s);
            arc.lineTo (c.x - 2 * s, c.y - 2 * s);
            arc.quadraticTo (c.x - 6 * s, c.y - 2 * s, c.x - 6 * s, c.y + 2 * s);
            arc.quadraticTo (c.x - 6 * s, c.y + 6 * s, c.x - 2 * s, c.y + 6 * s);
            arc.lineTo (c.x + 1 * s, c.y + 6 * s);
            p.addPath (arc);
        }
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void IconButton::mouseUp (const juce::MouseEvent& e)
    {
        if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }

    //==========================================================================
    void SnapshotButtons::paint (juce::Graphics& g)
    {
        const char* names[] = { "A", "B" };
        const float w = 24.0f, gap = 3.0f, y = ((float) getHeight() - w) / 2.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto r = juce::Rectangle<float> ((w + gap) * (float) i, y, w, w);
            g.setColour (i == active ? colours::accent : colours::panel2);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (i == active ? juce::Colours::white : colours::text2);
            g.setFont (font (11.0f, Weight::semibold));
            g.drawText (names[i], r, juce::Justification::centred, false);
        }
        // « A→B » : copier A dans B
        auto c = juce::Rectangle<float> (2.0f * (w + gap) + 4.0f, y, (float) getWidth() - (2.0f * (w + gap) + 4.0f), w);
        g.setColour (colours::panel2);
        g.fillRoundedRectangle (c, 7.0f);
        g.setColour (colours::text2);
        g.setFont (font (10.0f, Weight::semibold));
        g.drawText (juce::String::fromUTF8 ("A→B"), c, juce::Justification::centred, false);
    }

    void SnapshotButtons::mouseDown (const juce::MouseEvent& e)
    {
        if (e.position.x > 2.0f * 27.0f + 2.0f)
        {
            if (onCopy) onCopy();
            return;
        }
        const int i = juce::jlimit (0, 1, (int) (e.position.x / 27.0f));
        if (onSelect)
            onSelect (i);
    }

    //==========================================================================
    ParamSwitch::ParamSwitch (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& l)
        : attachment (s, paramId, button), label (l)
    {
        addAndMakeVisible (button);
    }

    void ParamSwitch::resized()
    {
        button.setBounds (getLocalBounds().removeFromRight (34));
    }

    void ParamSwitch::paint (juce::Graphics& g)
    {
        g.setColour (colours::text2);
        g.setFont (font (12.0f));
        g.drawText (label, getLocalBounds().withTrimmedRight (38), juce::Justification::centredLeft, true);
    }
}
