#include "Theme.h"
#include "BreathAssets.h"

namespace breath::ui
{
    namespace
    {
        struct Fonts
        {
            juce::Typeface::Ptr regular, medium, semibold;

            Fonts()
            {
                regular  = juce::Typeface::createSystemTypefaceFor (BreathAssets::InterRegular_otf,  BreathAssets::InterRegular_otfSize);
                medium   = juce::Typeface::createSystemTypefaceFor (BreathAssets::InterMedium_otf,   BreathAssets::InterMedium_otfSize);
                semibold = juce::Typeface::createSystemTypefaceFor (BreathAssets::InterSemiBold_otf, BreathAssets::InterSemiBold_otfSize);
            }

            static Fonts& get()
            {
                static Fonts f;
                return f;
            }
        };
    }

    juce::Font font (float height, Weight w)
    {
        auto& f = Fonts::get();
        auto tf = w == Weight::semibold ? f.semibold : (w == Weight::medium ? f.medium : f.regular);
        if (tf == nullptr)
            return juce::Font (juce::FontOptions (height, w == Weight::regular ? juce::Font::plain : juce::Font::bold));
        return juce::Font (juce::FontOptions (tf).withHeight (height));
    }

    juce::Colour heat (float db)
    {
        const float t = juce::jlimit (0.0f, 1.0f, db / 9.0f);
        struct Stop { float p; juce::Colour c; };
        static const Stop stops[] = {
            { 0.0f,  juce::Colour (0xff26262c) },
            { 0.5f,  juce::Colour (0xff0a84ff) },
            { 0.78f, juce::Colour (0xffbf5af2) },
            { 1.0f,  juce::Colour (0xffff455a) }
        };
        for (int i = 1; i < 4; ++i)
            if (t <= stops[i].p)
                return stops[i - 1].c.interpolatedWith (stops[i].c, (t - stops[i - 1].p) / (stops[i].p - stops[i - 1].p));
        return stops[3].c;
    }

    void drawCard (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 12.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    }

    void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j)
    {
        auto f = font (10.0f, Weight::semibold);
        f.setExtraKerningFactor (0.08f);
        g.setFont (f);
        g.setColour (colours::text3);
        g.drawText (text.toUpperCase(), r, j, false);
    }

    //==========================================================================
    LookAndFeel::LookAndFeel()
    {
        setColour (juce::Label::textColourId, colours::text);
        setColour (juce::Label::textWhenEditingColourId, colours::text);
        setColour (juce::Label::backgroundWhenEditingColourId, colours::panel2);
        setColour (juce::Label::outlineWhenEditingColourId, colours::accent);
        setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.4f));
        setColour (juce::TextEditor::highlightedTextColourId, colours::text);
        setColour (juce::CaretComponent::caretColourId, colours::accent);
        setColour (juce::PopupMenu::backgroundColourId, colours::panel2);
        setColour (juce::PopupMenu::textColourId, colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::TooltipWindow::backgroundColourId, colours::panel2);
        setColour (juce::TooltipWindow::textColourId, colours::text);
    }

    juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
    {
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        {
            auto& fonts = Fonts::get();
            auto tf = f.isBold() ? fonts.semibold : fonts.regular;
            if (tf != nullptr)
                return tf;
        }
        return LookAndFeel_V4::getTypefaceForFont (f);
    }

    void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
        const float cy = r.getCentreY();
        const float th = 4.0f;
        const bool enabled = s.isEnabled();
        const auto fill = enabled ? colours::accent : colours::text3;

        g.setColour (colours::line2);
        g.fillRoundedRectangle (r.getX(), cy - th / 2, r.getWidth(), th, th / 2);

        auto thumb = [&] (float px)
        {
            const auto c = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ px, cy });
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (c.translated (0.0f, 1.0f).expanded (0.5f));
            g.setColour (enabled ? juce::Colours::white : colours::text2);
            g.fillEllipse (c);
        };

        if (style == juce::Slider::TwoValueHorizontal)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (minPos, cy - th / 2, juce::jmax (0.0f, maxPos - minPos), th, th / 2);
            thumb (minPos);
            thumb (maxPos);
        }
        else
        {
            g.setColour (fill);
            g.fillRoundedRectangle (r.getX(), cy - th / 2, juce::jmax (0.0f, pos - r.getX()), th, th / 2);
            thumb (pos);
        }
    }

    void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
    {
        // Interrupteur façon macOS.
        auto r = b.getLocalBounds().toFloat();
        auto sw = juce::Rectangle<float> (30.0f, 18.0f).withCentre (r.getCentre());
        const bool on = b.getToggleState();
        g.setColour (on ? colours::accent : (highlighted ? colours::raised.brighter (0.1f) : colours::line2));
        g.fillRoundedRectangle (sw, 9.0f);
        const auto knob = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ on ? sw.getRight() - 9.0f : sw.getX() + 9.0f, sw.getCentreY() });
        g.setColour (juce::Colours::white);
        g.fillEllipse (knob);
    }

    void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat();
        auto c = b.getToggleState() ? colours::accent : colours::panel2;
        if (highlighted) c = c.brighter (0.08f);
        if (down) c = c.darker (0.1f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 7.0f);
    }

    void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.setColour (b.getToggleState() ? juce::Colours::white : colours::text2);
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        g.setColour (colours::panel2);
        g.fillRoundedRectangle (0.0f, 0.0f, (float) width, (float) height, 8.0f);
        g.setColour (colours::line2);
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f, 8.0f, 1.0f);
        juce::AttributedString s;
        s.append (text, font (12.0f), colours::text);
        s.setJustification (juce::Justification::centredLeft);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, (float) width - 20.0f);
        tl.draw (g, { 10.0f, 8.0f, (float) width - 20.0f, (float) height - 16.0f });
    }

    juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
    {
        juce::AttributedString s;
        s.append (tipText, font (12.0f), colours::text);
        s.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl;
        tl.createLayout (s, 260.0f);
        const int w = (int) std::ceil (tl.getWidth()) + 22;
        const int h = (int) std::ceil (tl.getHeight()) + 18;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 12,
                                     w, h).constrainedWithin (parentArea);
    }
}
