// Every control mapping AFTERHIT has, in one place. The engine, the host's
// parameter text, the dials and the timing strip all read these functions, so
// a read-out can never disagree with what the DSP is doing.

#pragma once

#include <algorithm>
#include <cmath>

namespace ah::law
{
    // --- ranges and defaults (spec section 4) -----------------------------------
    inline constexpr float kHitDef   = 20.0f;   // %
    inline constexpr float kSpaceDef = 28.0f;   // %
    inline constexpr float kTailMin  = 0.15f, kTailMax = 6.0f, kTailDef = 1.0f;   // s
    inline constexpr float kGateDef  = 40.0f;   // %
    inline constexpr float kAfterMax = 100.0f, kAfterDef = 25.0f;                 // ms
    inline constexpr float kSensDef  = 50.0f;   // %
    inline constexpr float kToneDef  = 45.0f;   // %
    inline constexpr float kWidthMax = 150.0f, kWidthDef = 100.0f;                // %
    inline constexpr float kOutMin   = -12.0f, kOutMax = 6.0f;                    // dB

    //  HIT at 100 % = this much attack emphasis on the dry signal.
    inline constexpr float kHitMaxDb = 6.0f;

    // --- HIT --------------------------------------------------------------------
    //  Linear: the dB of emphasis a full attack receives. 0 % is exactly 0 dB.
    inline float hitDb (float hitPct) { return kHitMaxDb * std::clamp (hitPct, 0.0f, 100.0f) * 0.01f; }

    // --- SPACE ------------------------------------------------------------------
    //  Wet-return gain. Square law: 10 % is -40 dB of the full-scale wet level,
    //  so the first few percent fade the room in rather than switching it on.
    //  kSpaceFullGain is calibrated (docs/DESIGN.md) so that the default 28 %
    //  keeps a real snare's tail peak ~7-8 dB under its hit and a short clap's ~19 dB under - clearly audible,
    //  never competing with the attack.
    inline constexpr float kSpaceFullGain = 6.0f;
    inline float spaceGain (float spacePct)
    {
        const float s = std::clamp (spacePct, 0.0f, 100.0f) * 0.01f;
        return kSpaceFullGain * s * s;
    }

    // --- TAIL -------------------------------------------------------------------
    //  A longer T60 holds more energy for the same hit. A partial level
    //  compensation (quarter power of the ratio) keeps a 0.15 s tail audible
    //  and stops a 6 s tail from swamping the mix, without flattening the
    //  natural sense that long rooms are bigger.
    inline float tailCompensation (float tailS)
    {
        return std::pow (1.0f / std::clamp (tailS, kTailMin, kTailMax), 0.25f);
    }

    // --- GATE -------------------------------------------------------------------
    //  One monotonic law for both SYNC modes (spec section 4):
    //
    //    gate <= 0.5 %      OFF - the reverb follows TAIL naturally
    //    0.5 .. 40 %        reference window 3000 ms -> 250 ms  (log)
    //    40 .. 100 %        reference window  250 ms ->  80 ms  (log)
    //
    //  "Reference" means milliseconds at 120 BPM. SYNC OFF uses the reference
    //  window directly. SYNC ON converts it to beats (ms / 500), snaps to the
    //  nearest division in log distance, and plays that division at the host
    //  tempo. The snap depends only on the knob, never on tempo or transport,
    //  so a stationary knob always names the same division.
    inline constexpr float kGateOffBelow = 0.5f;          // %
    inline constexpr float kGateLongMs   = 3000.0f;
    inline constexpr float kGateKneeMs   = 250.0f;
    inline constexpr float kGateKneePos  = 0.40f;
    inline constexpr float kGateShortMs  = 80.0f;
    inline constexpr double kFallbackBpm = 120.0;

    inline bool gateIsOff (float gatePct) { return gatePct < kGateOffBelow; }

    inline float gateReferenceMs (float gatePct)
    {
        const float u = std::clamp (gatePct, 0.0f, 100.0f) * 0.01f;
        if (u <= kGateKneePos)
            return kGateLongMs * std::pow (kGateKneeMs / kGateLongMs, u / kGateKneePos);
        return kGateKneeMs * std::pow (kGateShortMs / kGateKneeMs, (u - kGateKneePos) / (1.0f - kGateKneePos));
    }

    //  Inverse of gateReferenceMs, for typed-in values.
    inline float gatePctForReferenceMs (float ms)
    {
        ms = std::clamp (ms, kGateShortMs, kGateLongMs);
        if (ms >= kGateKneeMs)
            return 100.0f * kGateKneePos * std::log (ms / kGateLongMs) / std::log (kGateKneeMs / kGateLongMs);
        return 100.0f * (kGateKneePos + (1.0f - kGateKneePos) * std::log (ms / kGateKneeMs) / std::log (kGateShortMs / kGateKneeMs));
    }

    struct Division { float beats; const char* name; };
    inline constexpr Division kDivisions[] = {
        { 4.0f,   "1 bar" }, { 2.0f,   "1/2"   }, { 1.5f,   "1/4." }, { 1.0f,  "1/4" },
        { 0.75f,  "1/8."  }, { 0.5f,   "1/8"   }, { 0.375f, "1/16." }, { 0.25f, "1/16" },
        { 0.125f, "1/32"  } };
    inline constexpr int kNumDivisions = (int) (sizeof (kDivisions) / sizeof (kDivisions[0]));

    inline int gateDivisionIndex (float gatePct)
    {
        const float beats = gateReferenceMs (gatePct) / 500.0f;
        int best = 0;
        float bestD = 1.0e9f;
        for (int i = 0; i < kNumDivisions; ++i)
        {
            const float d = std::abs (std::log (beats / kDivisions[i].beats));
            if (d < bestD - 1.0e-6f) { bestD = d; best = i; }
        }
        return best;
    }

    inline double sanitiseBpm (double bpm)
    {
        return (std::isfinite (bpm) && bpm >= 20.0 && bpm <= 999.0) ? bpm : kFallbackBpm;
    }

    //  The audible window in ms, from the moment the wet return opens to the
    //  moment the close has faded it out. 0 = OFF.
    inline float gateWindowMs (float gatePct, bool sync, double bpm)
    {
        if (gateIsOff (gatePct))
            return 0.0f;
        if (! sync)
            return gateReferenceMs (gatePct);
        const double b = sanitiseBpm (bpm);
        return (float) (kDivisions[gateDivisionIndex (gatePct)].beats * 60000.0 / b);
    }

    //  How the window is spent: hold fully open, then a smooth close. The
    //  close is a critically damped two-pole fall whose time constant is a
    //  fifth of the fade, so the level is ~-28 dB at the window's end and
    //  still falling - never a step.
    inline float gateFadeMs (float windowMs) { return std::clamp (0.35f * windowMs, 12.0f, 90.0f); }
    inline float gateHoldMs (float windowMs) { return std::max (0.0f, windowMs - gateFadeMs (windowMs)); }

    // --- SENSITIVITY ------------------------------------------------------------
    //  Onset = the fast power envelope rising this far over the slow one AND
    //  crossing an absolute floor. Low sensitivity demands a big jump and a
    //  loud hit (ghost notes ignored); high catches soft percussion. The floor
    //  never goes below -66 dBFS, so noise under that never triggers.
    inline float onsetRiseDb (float sensPct)  { return 11.0f - 6.5f  * std::clamp (sensPct, 0.0f, 100.0f) * 0.01f; }
    inline float onsetFloorDb (float sensPct) { return -34.0f - 32.0f * std::clamp (sensPct, 0.0f, 100.0f) * 0.01f; }

    // --- TONE -------------------------------------------------------------------
    //  Wet only. Dark to bright: how fast the highs die relative to TAIL
    //  (above ~3 kHz the room decays in this fraction of T60), and the wet
    //  output's low-pass corner. TAIL stays the decay of the body of the room
    //  at every TONE setting.
    inline float toneHfDecayRatio (float tonePct) { return 0.2f + 0.7f * std::clamp (tonePct, 0.0f, 100.0f) * 0.01f; }
    inline float toneOutputHz (float tonePct) { return 3000.0f * std::pow (18000.0f / 3000.0f, std::clamp (tonePct, 0.0f, 100.0f) * 0.01f); }

    // --- timing constants -------------------------------------------------------
    //  How far back the send taps the input, so the part of the hit that came
    //  before the detector fired still reaches the reverb.
    inline constexpr float kLookbackMs   = 4.0f;
    //  The send stays fully open this long after an onset, then releases.
    inline constexpr float kSendHoldMs   = 40.0f;
    inline constexpr float kSendReleaseMs= 25.0f;   // one-pole time constant
    //  Return gate time constants (each of two cascaded one-poles).
    inline constexpr float kDuckTauMs    = 1.0f;
    inline constexpr float kOpenTauMs    = 2.0f;
    //  When GATE is on, a new hit chokes the tank holding the previous tail
    //  down to this T60, so rolls never pile up.
    inline constexpr float kChokeT60     = 0.12f;
}
