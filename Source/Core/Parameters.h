#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIds.h"
#include "../Dsp/AfterHitEngine.h"

namespace ah
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //  Reads every parameter (real-time safe: atomic loads only). Tempo is
    //  filled in by the processor from the play head.
    AfterHitEngine::Settings readSettings (const juce::AudioProcessorValueTreeState&) noexcept;

    //  Read-outs shared by the dials, the host's parameter text and the strip.
    juce::String formatPercent (float pct);
    juce::String formatTail (float seconds);
    juce::String formatMs (float ms);
    juce::String formatDb (float db);
    //  GATE as the dial shows it: OFF, a division (SYNC on) or milliseconds.
    juce::String formatGate (float gatePct, bool sync, double bpm);
    //  GATE as the host shows it (the host knows nothing about SYNC):
    //  "OFF" or "1/8 | 250 ms".
    juce::String formatGateForHost (float gatePct);
}
