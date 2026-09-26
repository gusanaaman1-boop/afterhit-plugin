// AFTERHIT's DSP, independent of JUCE's plug-in classes so the measurement
// suite can drive it directly.
//
// Signal flow (per sample, stereo-linked):
//
//   in ──┬──────────────── dry ── HIT shaper ─────────────────────┐
//        ├─ detector ── onset ──┬─ send window                    │
//        │                      ├─ return gate (duck/AFTER/GATE)  │
//        │                      └─ tank choke / route             │
//        └─ look-back 4 ms ─ HP ─ × send window ─ pre-delay        │
//               ─ diffuser ─ ER + 2 FDN tanks ─ TONE ─ WIDTH        │
//               ─ × SPACE × return gate ──────────────────────── (+) ─ OUTPUT ─ bypass xfade ─ out
//
// The dry path is never delayed: zero latency. The hit enters the reverb at
// once (via the look-back), but its reverb is heard only after AFTER.

#pragma once

#include <array>
#include <vector>

#include "Laws.h"
#include "Reverb.h"
#include "Telemetry.h"

namespace ah
{
    class AfterHitEngine
    {
    public:
        struct Settings
        {
            float hitPct   = law::kHitDef;
            float spacePct = law::kSpaceDef;
            float tailS    = law::kTailDef;
            float gatePct  = law::kGateDef;
            float afterMs  = law::kAfterDef;
            float sensPct  = law::kSensDef;
            float tonePct  = law::kToneDef;
            float widthPct = law::kWidthDef;
            bool  sync     = true;
            float outDb    = 0.0f;
            bool  bypass   = false;
            double bpm     = law::kFallbackBpm;
            bool  bpmFromHost = false;
        };

        //  Coefficients update every kSubBlock samples on a counter that runs
        //  across process() calls, so the output does not depend on how the
        //  host slices the stream into blocks.
        static constexpr int kSubBlock = 32;

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();

        void setSettings (const Settings& s) noexcept { settings = s; }
        const Settings& getSettings() const noexcept { return settings; }

        //  In place. numChannels 1 or 2.
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        Telemetry& getTelemetry() noexcept { return telemetry; }

        double getSampleRate() const noexcept { return fs; }
        //  What a host must keep rendering after the input stops.
        static float tailSecondsFor (const Settings&) noexcept;

        // --- test hooks -------------------------------------------------------
        int   getOnsetCount() const noexcept { return onsetCount; }
        float getReturnGain() const noexcept { return retG2; }
        float getSendLevel() const noexcept  { return sendEnv; }
        float getHitGainDb() const noexcept  { return lastHitDb; }
        float getWindowMs() const noexcept   { return windowNow; }
        int   getActiveTank() const noexcept { return reverb.activeTank(); }
        bool  isReverbAsleep() const noexcept { return reverb.isTankAsleep (0) && reverb.isTankAsleep (1); }

    private:
        struct Ramp
        {
            float cur = 0.0f, target = 0.0f, step = 0.0f;
            int left = 0, length = 1;
            void setLength (int n) noexcept { length = n < 1 ? 1 : n; }
            void snap (float v) noexcept { cur = target = v; left = 0; step = 0.0f; }
            void setTarget (float t) noexcept
            {
                if (t == target) return;
                target = t;
                left = length;
                step = (target - cur) / (float) length;
            }
            float next() noexcept
            {
                if (left > 0 && --left == 0) cur = target;
                else if (left > 0)           cur += step;
                return cur;
            }
        };

        enum class RetPhase { protect, open, closing };

        void updateSubBlock() noexcept;
        void onOnset() noexcept;
        float coefFor (float tauMs) const noexcept;

        Settings settings;
        Telemetry telemetry;
        Reverb reverb;

        double fs = 48000.0;
        int subPhase = 0;

        // detector
        float fastP = 0.0f, slowP = 0.0f;
        float cFastA = 0, cFastR = 0, cSlowA = 0, cSlowR = 0;
        float riseRatio = 1.0f, rearmRatio = 1.0f, floorP = 0.0f;
        bool  armed = true, refractory = false;
        int   refrCount = 0, refrMin = 1, refrMax = 1;
        float refrPeak = 0.0f;
        int   onsetCount = 0;

        // HIT shaper
        float envF = 0.0f, envS = 0.0f, attS = 0.0f;
        float cEnvFA = 0, cEnvFR = 0, cEnvSA = 0, cEnvSR = 0, cAtt = 0;
        float lastHitDb = 0.0f;
        Ramp hitRamp;

        // send
        std::array<std::vector<float>, 2> look;
        int lookLen = 1, lookPos = 0;
        float hpX[2] {}, hpY[2] {}, cHp = 0.0f;
        float sendEnv = 0.0f, cSendOpen = 0, cSendRel = 0;
        int sendHold = 0, sendHoldLen = 1;

        // pre-delay (on the send, so moving AFTER never bends the tail's pitch)
        std::array<std::vector<float>, 2> pre;
        int preLen = 1, prePos = 0;
        float preNow = 0.0f, preTarget = 0.0f;

        // wet
        float toneY[2] {}, cTone = 1.0f;
        float toneNow = law::kToneDef, tailNow = law::kTailDef, cParam = 0.0f;
        Ramp wetRamp, widthRamp;

        // return gate
        RetPhase retPhase = RetPhase::open;
        int retCount = 0, protectLen = 0, holdLen = 0;
        bool gateOn = true;
        float retG1 = 1.0f, retG2 = 1.0f, cDuck = 0, cOpen = 0, cClose = 0;
        float windowNow = 0.0f;

        // output
        Ramp outRamp, bypassRamp;

        // telemetry capture
        std::array<int, timeAxis::kPostBins + 1> binEnd {};
        std::array<float, timeAxis::kPreBins> preRing {};
        int preRingPos = 0, preBinLen = 1, preBinCount = 0;
        float preAcc = 0.0f;
        bool capActive = false;
        int capBin = 0, capSample = 0;
        float capDry = 0.0f, capWet = 0.0f;
    };
}
