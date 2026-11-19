#include "PluginEditor.h"

using namespace breath::ui;

BreathAudioProcessorEditor::BreathAudioProcessorEditor (BreathAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p),
      topBar (p), instanceView (p), matrixView (p),
      vblank (this, [this] { onFrame(); })
{
    // Lire la taille mémorisée AVANT toute mise en place (les limites de taille déclenchent un redimensionnement).
    const int remembered = processor.savedEditorWidth;

    setLookAndFeel (&lnf);
    tooltips.setLookAndFeel (&lnf);

    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addChildComponent (instanceView);
    content.addChildComponent (matrixView);
    topBar.onViewChange = [this] (int v) { setView (v, true); };

    content.setBounds (0, 0, designWidth, designHeight);
    topBar.setBounds (0, 0, designWidth, 52);
    const auto body = juce::Rectangle<int> (18, 68, designWidth - 36, designHeight - 68 - 18);
    instanceView.setBounds (body);
    matrixView.setBounds (body);

    setView (processor.currentView, false);

    // Redimensionnable, proportions conservées.
    setResizable (true, true);
    setResizeLimits (designWidth * 2 / 3, designHeight * 2 / 3, designWidth * 3, designHeight * 3);
    getConstrainer()->setFixedAspectRatio ((double) designWidth / (double) designHeight);
    const int w = juce::jlimit (designWidth * 2 / 3, designWidth * 3, remembered > 0 ? remembered : designWidth);
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
    auto* show = v == 0 ? static_cast<juce::Component*> (&instanceView) : &matrixView;
    auto* hide = v == 0 ? static_cast<juce::Component*> (&matrixView) : &instanceView;

    if (v == 1) matrixView.refresh(); else instanceView.refresh();

    if (animate)
    {
        auto& anim = juce::Desktop::getInstance().getAnimator();
        hide->setVisible (false);
        show->setAlpha (0.0f);
        show->setVisible (true);
        anim.fadeIn (show, 180);
    }
    else
    {
        hide->setVisible (false);
        show->setVisible (true);
    }
}

void BreathAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    topBar.refresh();
    instanceView.refresh();
    matrixView.refresh();
}

void BreathAudioProcessorEditor::onFrame()
{
    ++frame;
    if (instanceView.isVisible()) instanceView.tick();
    if (matrixView.isVisible() && (frame % 2) == 0) matrixView.tick();
    if ((frame % 2) == 0) topBar.tick();
    if ((frame % 30) == 0) { instanceView.repaint(); matrixView.repaint(); }
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
    g.fillAll (colours::bg);
}
