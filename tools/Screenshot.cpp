// Outil de développement : ouvre l'interface du plugin hors DAW et l'enregistre en PNG.
// Usage : BreathScreenshot <sortie.png> [largeur]
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main (int argc, char** argv)
{
    if (argc < 2)
        return 1;

    juce::ScopedJuceInitialiser_GUI gui;
    std::unique_ptr<juce::AudioProcessor> proc (createPluginFilter());
    proc->setPlayConfigDetails (2, 2, 48000.0, 256);
    proc->prepareToPlay (48000.0, 256);

    juce::AudioProcessor::TrackProperties tp;
    tp.name = juce::String ("Kick");
    tp.colourARGB = 0xffff6b4a;
    proc->updateTrackProperties (tp);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc->createEditorIfNeeded());
    if (argc > 2)
    {
        const int w = juce::String (argv[2]).getIntValue();
        editor->setSize (w, w * editor->getHeight() / editor->getWidth());
    }

    const float scale = 2.0f; // rendu « Retina »
    auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat().writeImageToStream (image, stream);
    editor.reset();
    proc.reset();
    return 0;
}
