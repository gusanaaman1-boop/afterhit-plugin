#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/Presets.h"

AfterHitAudioProcessor::AfterHitAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AFTERHIT", ah::createParameterLayout())
{
    engine.setSettings (ah::readSettings (apvts));
    engine.prepare (48000.0, 512, 2);
}

void AfterHitAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    //  Zero latency, reported explicitly: the dry path is never delayed.
    setLatencySamples (0);
    engine.setSettings (ah::readSettings (apvts));
    engine.prepare (sampleRate, samplesPerBlock, juce::jmax (1, getTotalNumOutputChannels()));
}

bool AfterHitAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

double AfterHitAudioProcessor::getTailLengthSeconds() const
{
    //  From the parameters, not the engine: hosts ask from any thread.
    const auto s = ah::readSettings (apvts);
    return (double) ah::AfterHitEngine::tailSecondsFor (s);
}

juce::AudioProcessorParameter* AfterHitAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ah::id::bypass);
}

void AfterHitAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    auto s = ah::readSettings (apvts);
    s.bpm = ah::law::kFallbackBpm;
    s.bpmFromHost = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm >= 20.0 && *bpm <= 999.0)
                {
                    s.bpm = *bpm;
                    s.bpmFromHost = true;
                }
    engine.setSettings (s);

    const int channels = juce::jmin (2, buffer.getNumChannels());
    if (channels > 0)
        engine.process (buffer.getArrayOfWritePointers(), channels, buffer.getNumSamples());
}

juce::AudioProcessorEditor* AfterHitAudioProcessor::createEditor()
{
    return new AfterHitAudioProcessorEditor (*this);
}

// --- programs -----------------------------------------------------------------
int AfterHitAudioProcessor::getNumPrograms() { return ah::presets::count(); }

void AfterHitAudioProcessor::setCurrentProgram (int index)
{
    //  Re-selecting the current program is a no-op. Hosts (Cubase among them)
    //  re-send the saved program number after restoring a project's state; if
    //  that re-applied the preset it would overwrite the user's restored
    //  settings. Restored state always wins.
    if (index < 0 || index >= ah::presets::count() || index == currentProgram.load())
        return;
    currentProgram.store (index);
    ah::presets::apply (index, apvts);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

void AfterHitAudioProcessor::applyPreset (int index)
{
    if (index < 0 || index >= ah::presets::count())
        return;
    currentProgram.store (index);
    ah::presets::apply (index, apvts);
    updateHostDisplay (ChangeDetails().withProgramChanged (true));
}

const juce::String AfterHitAudioProcessor::getProgramName (int index)
{
    return index >= 0 && index < ah::presets::count() ? ah::presets::name (index) : juce::String();
}

// --- state --------------------------------------------------------------------
void AfterHitAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", ah::id::stateVersion, nullptr);
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("advancedOpen", advancedOpen.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void AfterHitAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            currentProgram.store (juce::jlimit (0, ah::presets::count() - 1, (int) tree.getProperty ("program", 0)));
            advancedOpen.store ((bool) tree.getProperty ("advancedOpen", false));
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AfterHitAudioProcessor();
}
