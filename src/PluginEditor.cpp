#include "PluginEditor.h"

using namespace breath::ui;

BreathAudioProcessorEditor::BreathAudioProcessorEditor (BreathAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      topBar (p), instanceView (p), matrixView (p), ensembleView (p),
      vblank (this, [this] { onFrame(); })
{
    // Lire la taille mémorisée AVANT toute mise en place (les limites de taille déclenchent un redimensionnement).
    const int remembered = processor.savedEditorWidth;

    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    addAndMakeVisible (content);
    content.addAndMakeVisible (titleBar);
    content.addAndMakeVisible (topBar);
    content.addChildComponent (instanceView);
    content.addChildComponent (matrixView);
    content.addChildComponent (ensembleView);
    topBar.onViewChange = [this] (int v) { setView (v, true); };

    content.setBounds (0, 0, designWidth, designHeight);
    titleBar.setBounds (0, 0, designWidth, titleHeight);
    topBar.setBounds (0, titleHeight, designWidth, 52);
    const int bodyTop = titleHeight + 68;
    const auto body = juce::Rectangle<int> (18, bodyTop, designWidth - 36, designHeight - bodyTop - 18);
    instanceView.setBounds (body);
    matrixView.setBounds (body);
    ensembleView.setBounds (body);

    setView (processor.currentView, false);

    // Redimensionnable, proportions conservées.
    setResizable (true, true);
    setResizeLimits (designWidth * 2 / 3, designHeight * 2 / 3, designWidth * 3, designHeight * 3);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    const int w = juce::jlimit (designWidth * 2 / 3, designWidth * 3, remembered > 0 ? remembered : defaultWidth);
    setSize (w, juce::roundToInt (w * (double) designHeight / designWidth));
    ready = true;

    processor.getHub().addChangeListener (this);
}

BreathAudioProcessorEditor::~BreathAudioProcessorEditor()
{
    processor.getHub().removeChangeListener (this);
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void BreathAudioProcessorEditor::setView (int v, bool animate)
{
    processor.currentView = v;
    topBar.setView (v);
    juce::Component* views[] = { &instanceView, &matrixView, &ensembleView };
    v = juce::jlimit (0, 2, v);
    auto* show = views[v];
    if (v == 0) instanceView.refresh();
    else if (v == 1) matrixView.refresh();
    else ensembleView.refresh();
    for (auto* c : views)
        if (c != show) c->setVisible (false);

    if (animate)
    {
        auto& anim = juce::Desktop::getInstance().getAnimator();
        show->setAlpha (0.0f);
        show->setVisible (true);
        anim.fadeIn (show, 180);
    }
    else
    {
        show->setVisible (true);
    }
}

void BreathAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    topBar.refresh();
    instanceView.refresh();
    matrixView.refresh();
    ensembleView.refresh();
}

void BreathAudioProcessorEditor::onFrame()
{
    ++frame;
    if (instanceView.isVisible()) instanceView.tick();
    if (matrixView.isVisible() && (frame % 2) == 0) matrixView.tick();
    if (ensembleView.isVisible() && (frame % 2) == 0) ensembleView.tick();
    if ((frame % 2) == 0)
    {
        titleBar.accentTint = breath::BreathHub::familyColour (processor.getFamily());
        titleBar.tick();
    }
    if ((frame % 2) == 0) topBar.tick();
    if ((frame % 30) == 0)
    {
        instanceView.repaint();
        matrixView.repaint();
        if (processor.getFamily() != shownFamily) { shownFamily = processor.getFamily(); repaint(); topBar.refresh(); instanceView.refresh(); matrixView.refresh(); }
    }
}

void BreathAudioProcessorEditor::resized()
{
    const float scale = (float) getWidth() / (float) designWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    if (ready)
        processor.savedEditorWidth = getWidth();
}

void BreathAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Teinte très légère selon la famille de l'instance.
    g.fillAll (colours::bg.interpolatedWith (breath::BreathHub::familyColour (processor.getFamily()), 0.035f));
}
