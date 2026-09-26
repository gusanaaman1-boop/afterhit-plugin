// The thin hit -> gap -> bloom strip under the header.
//
// Two layers, both real:
//   * the MODEL: what the current HIT/SPACE/TAIL/GATE/AFTER settings do to a
//     reference hit, computed from the same laws the DSP uses. It moves the
//     moment a knob moves, with or without audio.
//   * the CAPTURE: the last detected hit's actual dry peak and actual wet
//     output, binned by the audio thread (Telemetry). When audio stops, the
//     last capture simply stays - nothing is animated that did not happen.
//
// Time runs left to right on a warped axis (timeAxis in Telemetry.h): 20 ms of
// pre-roll, then log-like past 30 ms, so a 25 ms gap and a 6 s tail both read.

#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "../Dsp/Telemetry.h"

namespace ah::ui
{
    class TimingStrip : public juce::Component
    {
    public:
        TimingStrip (juce::AudioProcessorValueTreeState&, Telemetry&);

        //  Called by the editor's timer; repaints only when something changed.
        void refresh();

        void paint (juce::Graphics&) override;

        //  Test hooks.
        std::uint32_t shownEvent() const noexcept { return shownEventId; }
        float xForMs (float ms) const noexcept;

    private:
        struct Model { float hit, space, tail, gate, after, windowMs; bool sync; };
        Model readModel() const;
        float modelWetDb (const Model&, float msAfterOnset) const;
        juce::Rectangle<float> plotArea() const;

        juce::AudioProcessorValueTreeState& state;
        Telemetry& telemetry;

        std::array<float, Telemetry::kBins> dry {}, wet {};
        int filled = 0;
        std::uint32_t shownEventId = 0;
        Model lastModel {};
    };
}
