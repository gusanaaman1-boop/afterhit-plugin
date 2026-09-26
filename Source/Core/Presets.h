// Factory presets, exposed as host programs and in the header's selector.
//
// Program 0 is the plug-in's default state exactly (spec section 4), so a
// fresh instance reads "WIDE CLAP" and is honest about it. OUTPUT is left
// where the user has it - a preset changes the sound, not the gain staging -
// except Neutral, which also returns OUTPUT to 0 dB so it is truly transparent.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIds.h"
#include "../Dsp/Laws.h"

namespace ah::presets
{
    struct Preset
    {
        const char* name;
        float hit, space, tail, gate, after, sens, tone, width;
        bool sync;
    };

    inline const Preset kPresets[] = {
        //  name          HIT  SPACE TAIL   GATE  AFTER SENS  TONE  WIDTH SYNC
        { "Wide Clap",    20,  28,   1.00f, 40,   25,   50,   45,   100,  true },   // = defaults
        { "Tight Kick",   35,  18,   0.45f, 70,   35,   45,   35,    70,  true },
        { "Short Snare",  25,  30,   0.70f, 55,   20,   50,   55,   100,  true },
        { "Perc Room",    10,  35,   0.60f,  0,   10,   65,   60,   110,  true },
        { "Dub Hit",      15,  45,   3.50f,  0,   40,   45,   30,   130,  true },
        { "Big Fill",     30,  40,   2.20f, 25,   20,   60,   50,   140,  true },
        { "Neutral",       0,   0,   1.00f, 40,   25,   50,   45,   100,  true },
    };

    inline int count() { return (int) std::size (kPresets); }
    inline juce::String name (int i) { return kPresets[i].name; }
    inline int neutralIndex() { return count() - 1; }

    inline void setPlain (juce::AudioProcessorValueTreeState& s, const char* pid, float plain)
    {
        if (auto* p = s.getParameter (pid))
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }

    //  Do the current parameters still equal preset i? (The selector marks
    //  an edited preset with " *".)
    inline bool matches (int i, const juce::AudioProcessorValueTreeState& s)
    {
        const auto& p = kPresets[i];
        auto near = [&s] (const char* pid, float v, float tol) { return std::abs (s.getRawParameterValue (pid)->load() - v) <= tol; };
        return near (id::hit, p.hit, 0.05f) && near (id::space, p.space, 0.05f) && near (id::tail, p.tail, 0.002f)
            && near (id::gate, p.gate, 0.05f) && near (id::after, p.after, 0.05f) && near (id::sensitivity, p.sens, 0.05f)
            && near (id::tone, p.tone, 0.05f) && near (id::width, p.width, 0.05f) && near (id::sync, p.sync ? 1.0f : 0.0f, 0.01f)
            && (i != neutralIndex() || near (id::output, 0.0f, 0.01f));
    }

    inline void apply (int i, juce::AudioProcessorValueTreeState& s)
    {
        const auto& p = kPresets[i];
        setPlain (s, id::hit, p.hit);
        setPlain (s, id::space, p.space);
        setPlain (s, id::tail, p.tail);
        setPlain (s, id::gate, p.gate);
        setPlain (s, id::after, p.after);
        setPlain (s, id::sensitivity, p.sens);
        setPlain (s, id::tone, p.tone);
        setPlain (s, id::width, p.width);
        setPlain (s, id::sync, p.sync ? 1.0f : 0.0f);
        if (i == neutralIndex())
            setPlain (s, id::output, 0.0f);
    }
}
