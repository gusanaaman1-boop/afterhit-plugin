#include "Parameters.h"

namespace ah
{
    juce::String formatPercent (float pct) { return juce::String ((int) std::round (pct)) + "%"; }

    juce::String formatTail (float s)
    {
        if (s < 0.995f)
            return juce::String (s, 2) + " s";
        return juce::String (s, 1) + " s";
    }

    juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f)
            return juce::String (ms / 1000.0f, 2) + " s";
        return juce::String ((int) std::round (ms)) + " ms";
    }

    juce::String formatDb (float db)
    {
        if (std::abs (db) < 0.05f) db = 0.0f;
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }

    juce::String formatGate (float gatePct, bool sync, double bpm)
    {
        if (law::gateIsOff (gatePct))
            return "OFF";
        if (sync)
            return law::kDivisions[law::gateDivisionIndex (gatePct)].name;
        return formatMs (law::gateWindowMs (gatePct, false, bpm));
    }

    juce::String formatGateForHost (float gatePct)
    {
        if (law::gateIsOff (gatePct))
            return "OFF";
        return juce::String (law::kDivisions[law::gateDivisionIndex (gatePct)].name) + " | "
             + formatMs (law::gateReferenceMs (gatePct));
    }

    namespace
    {
        float parseNumber (const juce::String& t) { return t.retainCharacters ("0123456789.-").getFloatValue(); }

        float parseGate (const juce::String& text)
        {
            const auto u = text.trim().toUpperCase();
            if (u.isEmpty() || u.startsWith ("OFF"))
                return 0.0f;
            //  A division name: land in the middle of that division's band.
            const auto head = u.upToFirstOccurrenceOf ("|", false, false).trim().toLowerCase();
            for (int i = 0; i < law::kNumDivisions; ++i)
                if (head == juce::String (law::kDivisions[i].name))
                {
                    float lo = -1.0f, hi = -1.0f;
                    for (int k = 1; k <= 1000; ++k)
                    {
                        const float g = (float) k * 0.1f;
                        if (law::gateDivisionIndex (g) == i) { if (lo < 0.0f) lo = g; hi = g; }
                    }
                    return lo < 0.0f ? 0.0f : 0.5f * (lo + hi);
                }
            if (u.contains ("MS"))
                return law::gatePctForReferenceMs (parseNumber (u.upToFirstOccurrenceOf ("MS", false, false)));
            if (u.endsWith (" S") || u.endsWith ("S"))
                return law::gatePctForReferenceMs (1000.0f * parseNumber (u));
            return juce::jlimit (0.0f, 100.0f, parseNumber (u));
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using P = juce::AudioParameterFloat;
        using A = juce::AudioParameterFloatAttributes;
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

        auto ver = [] (const char* s) { return juce::ParameterID (s, id::stateVersion); };

        auto pctAttr = A().withStringFromValueFunction ([] (float v, int) { return formatPercent (v); })
                          .withValueFromStringFunction ([] (const juce::String& t) { return parseNumber (t); })
                          .withLabel ("%");
        auto pct = [] (float maxPct) { return juce::NormalisableRange<float> (0.0f, maxPct, 0.0f); };

        // --- the four -------------------------------------------------------------
        p.push_back (std::make_unique<P> (ver (id::hit),   "Hit",   pct (100.0f), law::kHitDef,   pctAttr));
        p.push_back (std::make_unique<P> (ver (id::space), "Space", pct (100.0f), law::kSpaceDef, pctAttr));

        juce::NormalisableRange<float> tailRange (law::kTailMin, law::kTailMax);
        tailRange.setSkewForCentre (std::sqrt (law::kTailMin * law::kTailMax));   // log-like travel
        p.push_back (std::make_unique<P> (ver (id::tail), "Tail", tailRange, law::kTailDef,
            A().withStringFromValueFunction ([] (float v, int) { return formatTail (v); })
               .withValueFromStringFunction ([] (const juce::String& t)
               {
                   const auto u = t.trim().toUpperCase();
                   const float v = parseNumber (u);
                   return juce::jlimit (law::kTailMin, law::kTailMax, u.contains ("MS") ? v * 0.001f : v);
               })
               .withLabel ("s")));

        p.push_back (std::make_unique<P> (ver (id::gate), "Gate", pct (100.0f), law::kGateDef,
            A().withStringFromValueFunction ([] (float v, int) { return formatGateForHost (v); })
               .withValueFromStringFunction ([] (const juce::String& t) { return parseGate (t); })));

        // --- ADVANCED ---------------------------------------------------------------
        p.push_back (std::make_unique<P> (ver (id::after), "After",
            juce::NormalisableRange<float> (0.0f, law::kAfterMax, 0.0f), law::kAfterDef,
            A().withStringFromValueFunction ([] (float v, int) { return formatMs (v); })
               .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (0.0f, law::kAfterMax, parseNumber (t)); })
               .withLabel ("ms")));
        p.push_back (std::make_unique<P> (ver (id::sensitivity), "Sensitivity", pct (100.0f), law::kSensDef, pctAttr));
        p.push_back (std::make_unique<P> (ver (id::tone),  "Tone",  pct (100.0f), law::kToneDef, pctAttr));
        p.push_back (std::make_unique<P> (ver (id::width), "Width", pct (law::kWidthMax), law::kWidthDef, pctAttr));
        p.push_back (std::make_unique<juce::AudioParameterBool> (ver (id::sync), "Sync", true,
            juce::AudioParameterBoolAttributes().withStringFromValueFunction ([] (bool v, int) { return juce::String (v ? "On" : "Off"); })));
        p.push_back (std::make_unique<P> (ver (id::output), "Output",
            juce::NormalisableRange<float> (law::kOutMin, law::kOutMax, 0.0f), 0.0f,
            A().withStringFromValueFunction ([] (float v, int) { return formatDb (v); })
               .withValueFromStringFunction ([] (const juce::String& t) { return juce::jlimit (law::kOutMin, law::kOutMax, parseNumber (t)); })
               .withLabel ("dB")));

        p.push_back (std::make_unique<juce::AudioParameterBool> (ver (id::bypass), "Bypass", false));

        return { p.begin(), p.end() };
    }

    AfterHitEngine::Settings readSettings (const juce::AudioProcessorValueTreeState& s) noexcept
    {
        auto get = [&s] (const char* pid) { return s.getRawParameterValue (pid)->load (std::memory_order_relaxed); };

        AfterHitEngine::Settings st;
        st.hitPct   = get (id::hit);
        st.spacePct = get (id::space);
        st.tailS    = get (id::tail);
        st.gatePct  = get (id::gate);
        st.afterMs  = get (id::after);
        st.sensPct  = get (id::sensitivity);
        st.tonePct  = get (id::tone);
        st.widthPct = get (id::width);
        st.sync     = get (id::sync) > 0.5f;
        st.outDb    = get (id::output);
        st.bypass   = get (id::bypass) > 0.5f;
        return st;
    }
}
