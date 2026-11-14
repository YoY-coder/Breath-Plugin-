#include "PluginEditor.h"

namespace
{
    // Palette neutre, sombre, un seul accent (bleu système Apple).
    const juce::Colour background   { 0xff1c1c1e };
    const juce::Colour card         { 0xff2c2c2e };
    const juce::Colour textPrimary  { 0xfff5f5f7 };
    const juce::Colour textSecondary{ 0xff8e8e93 };
    const juce::Colour accent       { 0xff0a84ff };
}

BreathAudioProcessorEditor::BreathAudioProcessorEditor (BreathAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // Redimensionnable, proportions conservées.
    setResizable (true, true);
    setResizeLimits (defaultWidth / 2, defaultHeight / 2, defaultWidth * 3, defaultHeight * 3);
    getConstrainer()->setFixedAspectRatio ((double) defaultWidth / (double) defaultHeight);

    const int w = processor.savedEditorWidth > 0 ? processor.savedEditorWidth : defaultWidth;
    setSize (w, juce::roundToInt (w * (double) defaultHeight / defaultWidth));

    startTimerHz (10);
}

void BreathAudioProcessorEditor::timerCallback()
{
    const auto info = processor.getTrackInfo();
    if (info.name != shownTrackInfo.name || info.colour != shownTrackInfo.colour)
    {
        shownTrackInfo = info;
        repaint();
    }
}

void BreathAudioProcessorEditor::resized()
{
    processor.savedEditorWidth = getWidth();
}

void BreathAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Tout est dessiné relativement à la taille → net à toutes les tailles.
    const float s = (float) getWidth() / (float) defaultWidth;
    auto bounds = getLocalBounds().toFloat();

    g.fillAll (background);

    // Titre
    auto content = bounds.reduced (40.0f * s);
    g.setColour (textPrimary);
    g.setFont (juce::FontOptions (34.0f * s, juce::Font::bold));
    g.drawText ("Breath", content.removeFromTop (44.0f * s), juce::Justification::centredLeft);

    g.setColour (textSecondary);
    g.setFont (juce::FontOptions (15.0f * s));
    g.drawText (juce::String::fromUTF8 ("Étape 1 — instance seule · version ") + JucePlugin_VersionString,
                content.removeFromTop (24.0f * s), juce::Justification::centredLeft);

    content.removeFromTop (28.0f * s);

    // Carte d'informations
    auto infoCard = content.removeFromTop (190.0f * s);
    g.setColour (card);
    g.fillRoundedRectangle (infoCard, 12.0f * s);

    auto rows = infoCard.reduced (24.0f * s, 20.0f * s);
    const float rowH = 30.0f * s;

    auto drawRow = [&] (const juce::String& label, const juce::String& value, std::optional<juce::Colour> dot = {})
    {
        auto row = rows.removeFromTop (rowH);
        g.setFont (juce::FontOptions (15.0f * s));
        g.setColour (textSecondary);
        g.drawText (label, row.removeFromLeft (220.0f * s), juce::Justification::centredLeft);

        if (dot.has_value())
        {
            auto d = row.removeFromLeft (rowH).withSizeKeepingCentre (12.0f * s, 12.0f * s);
            g.setColour (*dot);
            g.fillEllipse (d);
        }
        g.setColour (textPrimary);
        g.drawText (value, row, juce::Justification::centredLeft);
    };

    const juce::PluginHostType host;
    drawRow ("DAW", host.getHostDescription());

    const auto sr = processor.currentSampleRate.load();
    const auto bs = processor.currentBlockSize.load();
    drawRow ("Audio", sr > 0 ? juce::String (sr / 1000.0, 1) + " kHz  ·  buffer " + juce::String (bs)
                             : juce::String ("en attente"));

    if (processor.trackInfoReceived.load())
    {
        drawRow ("Nom de piste (DAW)", shownTrackInfo.name.isNotEmpty() ? shownTrackInfo.name
                                                                       : juce::String::fromUTF8 ("— (non fourni)"));
        drawRow ("Couleur de piste (DAW)",
                 shownTrackInfo.colour.has_value() ? shownTrackInfo.colour->toDisplayString (false)
                                                   : juce::String::fromUTF8 ("— (non fournie)"),
                 shownTrackInfo.colour);
    }
    else
    {
        drawRow ("Nom de piste (DAW)", juce::String::fromUTF8 ("— (le DAW n'a rien envoyé)"));
        drawRow ("Couleur de piste (DAW)", juce::String::fromUTF8 ("—"));
    }

    drawRow ("Latence", "0 sample");

    // Petit accent discret en bas
    g.setColour (accent);
    g.fillRoundedRectangle (bounds.getX() + 40.0f * s, bounds.getBottom() - 40.0f * s, 36.0f * s, 4.0f * s, 2.0f * s);
}
