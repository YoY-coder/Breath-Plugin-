#pragma once

#include "PluginProcessor.h"
#include "ui/Theme.h"
#include "ui/TopBar.h"
#include "ui/InstanceView.h"
#include "ui/MatrixView.h"

class BreathAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::ChangeListener
{
public:
    explicit BreathAudioProcessorEditor (BreathAudioProcessor&);
    ~BreathAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void showView (int v) { setView (v, false); }

    static constexpr int designWidth  = 900;
    static constexpr int designHeight = 560;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void onFrame();
    void setView (int v, bool animate);

    BreathAudioProcessor& processor;
    breath::ui::LookAndFeel lnf;

    // Tout est dessiné dans un repère fixe 900 × 560, mis à l'échelle (rendu vectoriel net).
    juce::Component content;
    breath::ui::TopBar topBar;
    breath::ui::InstanceView instanceView;
    breath::ui::MatrixView matrixView;
    juce::TooltipWindow tooltips { this, 700 };
    juce::VBlankAttachment vblank;
    int frame = 0;
    bool ready = false;   // taille initiale appliquée

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreathAudioProcessorEditor)
};
