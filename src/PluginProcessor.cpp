#include "PluginProcessor.h"
#include "PluginEditor.h"

BreathAudioProcessor::BreathAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void BreathAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    setLatencySamples (0);
}

bool BreathAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono ou stéréo, avec autant de canaux en entrée qu'en sortie.
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void BreathAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Étape 1 : le son passe sans modification.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* BreathAudioProcessor::createEditor()
{
    return new BreathAudioProcessorEditor (*this);
}

void BreathAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("BreathState");
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("editorWidth", savedEditorWidth, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BreathAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        const auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType ("BreathState"))
            savedEditorWidth = state.getProperty ("editorWidth", 0);
    }
}

void BreathAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const juce::ScopedLock sl (trackInfoLock);
    trackInfo.name = properties.name.value_or (juce::String());
    if (properties.colourARGB.has_value())
        trackInfo.colour = juce::Colour (*properties.colourARGB);
    else
        trackInfo.colour.reset();
    trackInfoReceived = true;
}

TrackInfo BreathAudioProcessor::getTrackInfo() const
{
    const juce::ScopedLock sl (trackInfoLock);
    return trackInfo;
}

// Point d'entrée appelé par le DAW pour créer une instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreathAudioProcessor();
}
