#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Core/Parameters.h"
#include "Dsp/AfterHitEngine.h"

class AfterHitAudioProcessor : public juce::AudioProcessor
{
public:
    AfterHitAudioProcessor();
    ~AfterHitAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

   #if defined (JucePlugin_Name)
    const juce::String getName() const override { return JucePlugin_Name; }
   #else
    const juce::String getName() const override { return "AFTERHIT"; }
   #endif
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    //  The host's bypass switch drives this parameter, so bypass is a
    //  click-free crossfade inside the plug-in and is automatable.
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    //  From the editor's selector: always applies, even the current preset
    //  (so "reload the preset I've been tweaking" works).
    void applyPreset (int index);

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    ah::AfterHitEngine& getEngine() noexcept { return engine; }
    ah::Telemetry& getTelemetry() noexcept { return engine.getTelemetry(); }

    //  Editor-only state, saved with the project: is ADVANCED open.
    std::atomic<bool> advancedOpen { false };

private:
    juce::AudioProcessorValueTreeState apvts;
    ah::AfterHitEngine engine;
    std::atomic<int> currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AfterHitAudioProcessor)
};
