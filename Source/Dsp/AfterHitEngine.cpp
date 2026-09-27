#include "AfterHitEngine.h"

#include <algorithm>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace ah
{
    namespace
    {
        constexpr float kLn10Over20 = 0.11512925464970229f;
        constexpr float kTwoPi = 6.28318530717958647692f;

        //  Detector envelopes (power domain, stereo-linked).
        constexpr float kFastAttackMs = 0.3f,  kFastReleaseMs = 8.0f;
        constexpr float kSlowAttackMs = 10.0f, kSlowReleaseMs = 12.0f;
        //  Refractory: at least 15 ms; ends once the hit has fallen 6 dB from
        //  its peak, and never lasts past 50 ms - so a long hit is one event
        //  and a fast roll is still many.
        constexpr float kRefrMinMs = 15.0f, kRefrMaxMs = 50.0f;

        //  HIT shaper envelopes (amplitude): a fast peak follower, and a slow
        //  follower OF the fast one. In steady material (a noisy clap body
        //  included) the two agree; only while an attack is under way does the
        //  fast one run ahead - so only the leading edge is lifted.
        constexpr float kShFastAMs = 0.2f, kShSlowAMs = 12.0f, kShRelMs = 30.0f, kShSmoothMs = 1.0f;

        constexpr float kSendHpHz = 60.0f;

        //  Recursive states are snapped to zero below -400 dB. Flush-to-zero
        //  alone is not enough: on x86 a one-pole like y += c * (0 - y) can
        //  stall on a tiny NORMAL value once c * y underflows to 0, and then
        //  the output never reaches exact silence.
        inline float snap (float v) noexcept { return std::abs (v) < 1.0e-20f ? 0.0f : v; }
        constexpr float kPreSlewPerSample = 0.05f;   // max pre-delay change, samples/sample
    }

    float AfterHitEngine::coefFor (float tauMs) const noexcept
    {
        return (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-3, (double) tauMs) * 0.001 * fs)));
    }

    void AfterHitEngine::prepare (double sampleRate, int /*maxBlockSize*/, int /*numChannels*/)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;

        cFastA = coefFor (kFastAttackMs); cFastR = coefFor (kFastReleaseMs);
        cSlowA = coefFor (kSlowAttackMs); cSlowR = coefFor (kSlowReleaseMs);
        refrMin = (int) std::lround (kRefrMinMs * 0.001 * fs);
        refrMax = (int) std::lround (kRefrMaxMs * 0.001 * fs);

        cEnvFA = coefFor (kShFastAMs); cEnvFR = coefFor (kShRelMs);
        cEnvSA = coefFor (kShSlowAMs); cEnvSR = coefFor (kShRelMs);
        cAtt   = coefFor (kShSmoothMs);

        lookLen = (int) std::lround (law::kLookbackMs * 0.001 * fs);
        for (auto& b : look) b.assign ((size_t) lookLen + 1, 0.0f);
        cHp = (float) std::exp (-kTwoPi * kSendHpHz / fs);
        cSendOpen = coefFor (0.3f);
        cSendRel  = coefFor (law::kSendReleaseMs);
        sendHoldLen = (int) std::lround ((law::kSendHoldMs + law::kLookbackMs) * 0.001 * fs);

        preLen = (int) std::ceil (law::kAfterMax * 0.001 * fs) + 8;
        for (auto& b : pre) b.assign ((size_t) preLen, 0.0f);

        cDuck = coefFor (law::kDuckTauMs);
        cOpen = coefFor (law::kOpenTauMs);
        cParam = (float) (1.0 - std::exp (-(double) kSubBlock / (0.030 * fs)));   // 30 ms, per sub-block

        const int ramp20 = (int) std::lround (0.020 * fs);
        hitRamp.setLength (ramp20);
        wetRamp.setLength ((int) std::lround (0.030 * fs));
        widthRamp.setLength (ramp20);
        outRamp.setLength (ramp20);
        bypassRamp.setLength ((int) std::lround (0.010 * fs));

        //  Telemetry bins on the strip's time axis.
        for (int k = 0; k <= timeAxis::kPostBins; ++k)
        {
            const float ms = timeAxis::fromUnit ((float) k / (float) timeAxis::kPostBins);
            binEnd[(size_t) k] = std::max (k, (int) std::lround (ms * 0.001 * fs));
        }
        preBinLen = std::max (1, (int) std::lround (timeAxis::kPreMs * 0.001 * fs / timeAxis::kPreBins));

        reverb.prepare (fs);
        reset();
    }

    void AfterHitEngine::reset()
    {
        reverb.clear();
        fastP = slowP = 0.0f;
        armed = true; refractory = false; refrCount = 0; refrPeak = 0.0f;
        envF = envS = attS = 0.0f;
        for (auto& b : look) std::fill (b.begin(), b.end(), 0.0f);
        lookPos = 0;
        for (int c = 0; c < 2; ++c) { hpX[c] = hpY[c] = 0.0f; toneY[c] = 0.0f; }
        sendEnv = 0.0f; sendHold = 0;
        for (auto& b : pre) std::fill (b.begin(), b.end(), 0.0f);
        prePos = 0;

        retPhase = RetPhase::open; retCount = 0; retG1 = retG2 = 1.0f;
        subPhase = 0;

        //  Land every smoothed value on its target, so a fresh instance (or a
        //  new sample rate) starts exactly where the parameters are.
        const auto& s = settings;
        tailNow = std::clamp (s.tailS, law::kTailMin, law::kTailMax);
        toneNow = s.tonePct;
        preNow = preTarget = std::max (0.0f, s.afterMs - law::kLookbackMs) * 0.001f * (float) fs;
        windowNow = law::gateWindowMs (s.gatePct, s.sync, s.bpm);
        hitRamp.snap (law::hitDb (s.hitPct));
        wetRamp.snap (law::spaceGain (s.spacePct) * law::tailCompensation (tailNow));
        widthRamp.snap (std::clamp (s.widthPct, 0.0f, law::kWidthMax) * 0.01f);
        outRamp.snap (std::pow (10.0f, s.outDb / 20.0f));
        bypassRamp.snap (s.bypass ? 1.0f : 0.0f);

        preRing.fill (0.0f); preRingPos = 0; preBinCount = 0; preAcc = 0.0f;
        capActive = false;
        updateSubBlock();
    }

    float AfterHitEngine::tailSecondsFor (const Settings& s) noexcept
    {
        return std::clamp (s.tailS, law::kTailMin, law::kTailMax) * 1.2f + std::clamp (s.afterMs, 0.0f, law::kAfterMax) * 0.001f + 0.1f;
    }

    void AfterHitEngine::updateSubBlock() noexcept
    {
        const auto& s = settings;

        //  Detector thresholds.
        riseRatio  = std::pow (10.0f, law::onsetRiseDb (s.sensPct) / 10.0f);
        rearmRatio = std::pow (10.0f, law::onsetRiseDb (s.sensPct) / 20.0f);
        floorP     = std::pow (10.0f, law::onsetFloorDb (s.sensPct) / 10.0f);

        hitRamp.setTarget (law::hitDb (s.hitPct));

        const float tailTarget = std::clamp (s.tailS, law::kTailMin, law::kTailMax);
        tailNow = std::exp (std::log (tailNow) + cParam * (std::log (tailTarget) - std::log (tailNow)));
        if (std::abs (tailNow - tailTarget) < 1.0e-5f) tailNow = tailTarget;
        reverb.setDecay (tailNow);
        reverb.updateCoefficients();

        toneNow += cParam * (s.tonePct - toneNow);
        reverb.setHfDecayRatio (law::toneHfDecayRatio (toneNow));
        cTone = (float) (1.0 - std::exp (-kTwoPi * std::min (law::toneOutputHz (toneNow), 0.45f * (float) fs) / fs));

        //  SPACE exactly 0 must give a wet gain of exactly 0.
        wetRamp.setTarget (s.spacePct <= 0.0f ? 0.0f : law::spaceGain (s.spacePct) * law::tailCompensation (tailNow));
        widthRamp.setTarget (std::clamp (s.widthPct, 0.0f, law::kWidthMax) * 0.01f);
        outRamp.setTarget (std::pow (10.0f, (std::abs (s.outDb) < 1.0e-3f ? 0.0f : std::clamp (s.outDb, law::kOutMin, law::kOutMax)) / 20.0f));
        bypassRamp.setTarget (s.bypass ? 1.0f : 0.0f);

        const float after = std::clamp (s.afterMs, 0.0f, law::kAfterMax);
        preTarget = std::max (0.0f, after - law::kLookbackMs) * 0.001f * (float) fs;

        //  GATE window. Smoothed while on, so tempo and knob moves glide; OFF
        //  is immediate (it only stops the close from happening).
        const float wTarget = law::gateWindowMs (s.gatePct, s.sync, s.bpm);
        gateOn = wTarget > 0.0f;
        if (! gateOn)               windowNow = 0.0f;
        else if (windowNow <= 0.0f) windowNow = wTarget;
        else                        windowNow += cParam * (wTarget - windowNow);
        if (gateOn)
        {
            holdLen = (int) std::lround (law::gateHoldMs (windowNow) * 0.001 * fs);
            cClose  = coefFor (law::gateFadeMs (windowNow) / 5.0f);
        }

        reverb.housekeep();

        telemetry.windowMs.store (windowNow, std::memory_order_relaxed);
        telemetry.afterMs.store (after, std::memory_order_relaxed);
        telemetry.bpm.store (law::sanitiseBpm (s.bpm), std::memory_order_relaxed);
        telemetry.bpmFromHost.store (s.bpmFromHost, std::memory_order_relaxed);
    }

    void AfterHitEngine::onOnset() noexcept
    {
        ++onsetCount;
        telemetry.onsets.store (onsetCount, std::memory_order_relaxed);

        //  1. Open the send: the look-back tap means the part of the hit that
        //     came before the detector fired is captured too.
        sendHold = sendHoldLen;

        //  2. Duck whatever wet is sounding, keep it down for AFTER, then open.
        retPhase = RetPhase::protect;
        retCount = 0;
        protectLen = (int) std::lround (std::clamp (settings.afterMs, 0.0f, law::kAfterMax) * 0.001 * fs);

        //  3. With GATE on, choke the old tail's tank; the new hit gets the
        //     other one as it leaves the pre-delay.
        reverb.beginEvent (gateOn, (int) std::lround (preNow));

        //  4. New capture for the timing strip: pre-roll first.
        for (int k = 0; k < timeAxis::kPreBins; ++k)
        {
            const float v = preRing[(size_t) ((preRingPos + k) % timeAxis::kPreBins)];
            telemetry.dryPeak[(size_t) k].store (v, std::memory_order_relaxed);
            telemetry.wetPeak[(size_t) k].store (0.0f, std::memory_order_relaxed);
        }
        telemetry.filled.store (timeAxis::kPreBins, std::memory_order_relaxed);
        telemetry.eventId.fetch_add (1, std::memory_order_release);
        capActive = true;
        capBin = 0; capSample = 0; capDry = 0.0f; capWet = 0.0f;
    }

    void AfterHitEngine::process (float* const* ch, int numChannels, int numSamples) noexcept
    {
        juce::ScopedNoDenormals noDenormals;

        if (numChannels <= 0 || numSamples <= 0)
            return;
        const bool stereo = numChannels >= 2;
        float* c0 = ch[0];
        float* c1 = stereo ? ch[1] : nullptr;

        float mDryL = 0, mDryR = 0, mWetL = 0, mWetR = 0, mOutL = 0, mOutR = 0;

        for (int n = 0; n < numSamples; ++n)
        {
            if (subPhase == 0)
                updateSubBlock();
            if (++subPhase >= kSubBlock)
                subPhase = 0;

            const float x0 = c0[n];
            const float x1 = stereo ? c1[n] : x0;

            // --- detector ---------------------------------------------------------
            float p = stereo ? 0.5f * (x0 * x0 + x1 * x1) : x0 * x0;
            if (! std::isfinite (p)) p = 0.0f;
            fastP = snap (fastP + (p > fastP ? cFastA : cFastR) * (p - fastP));
            slowP = snap (slowP + (p > slowP ? cSlowA : cSlowR) * (p - slowP));
            const float ratio = fastP / (slowP + 1.0e-12f);

            if (refractory)
            {
                ++refrCount;
                refrPeak = std::max (refrPeak, fastP);
                if ((refrCount >= refrMin && fastP < 0.25f * refrPeak) || refrCount >= refrMax)
                    refractory = false;
            }
            if (! armed && ! refractory && ratio < rearmRatio)
                armed = true;
            if (armed && ! refractory && ratio > riseRatio && fastP > floorP)
            {
                armed = false;
                refractory = true;
                refrCount = 0;
                refrPeak = fastP;
                onOnset();
            }

            // --- HIT: attack emphasis on the dry signal ----------------------------
            float d0 = x0, d1 = x1;
            {
                const float a = std::max (std::abs (x0), std::abs (x1));
                envF = snap (envF + (a > envF ? cEnvFA : cEnvFR) * (a - envF));
                envS = snap (envS + (envF > envS ? cEnvSA : cEnvSR) * (envF - envS));
                const float att = std::clamp (envF / (envS + 1.0e-9f) - 1.0f, 0.0f, 1.0f);
                attS = snap (attS + cAtt * (att - attS));
                const float hitDb = hitRamp.next();
                if (hitDb != 0.0f)
                {
                    lastHitDb = hitDb * attS;
                    const float g = std::exp (kLn10Over20 * lastHitDb);
                    d0 *= g;
                    d1 *= g;
                }
                else
                {
                    lastHitDb = 0.0f;   // bit-exact neutral: no arithmetic on the dry path
                }
            }

            // --- send: look-back tap, high-pass, event window ------------------------
            look[0][(size_t) lookPos] = x0;
            look[1][(size_t) lookPos] = x1;
            int lp = lookPos - lookLen; if (lp < 0) lp += lookLen + 1;
            const float xl0 = look[0][(size_t) lp], xl1 = look[1][(size_t) lp];
            if (++lookPos > lookLen) lookPos = 0;

            float s0, s1;
            {
                const float h0 = cHp * (hpY[0] + xl0 - hpX[0]);
                const float h1 = cHp * (hpY[1] + xl1 - hpX[1]);
                hpX[0] = xl0; hpX[1] = xl1;
                hpY[0] = std::isfinite (h0) ? snap (h0) : 0.0f;
                hpY[1] = std::isfinite (h1) ? snap (h1) : 0.0f;

                float tgt = 0.0f;
                if (sendHold > 0) { --sendHold; tgt = 1.0f; }
                sendEnv += (tgt > sendEnv ? cSendOpen : cSendRel) * (tgt - sendEnv);
                if (tgt == 0.0f && sendEnv < 1.0e-6f) sendEnv = 0.0f;
                s0 = hpY[0] * sendEnv;
                s1 = hpY[1] * sendEnv;
            }

            // --- pre-delay (fractional, slew-limited) ------------------------------
            pre[0][(size_t) prePos] = s0;
            pre[1][(size_t) prePos] = s1;
            {
                const float diff = preTarget - preNow;
                preNow += std::clamp (diff, -kPreSlewPerSample, kPreSlewPerSample);
            }
            float r0, r1;
            {
                float rp = (float) prePos - preNow;
                while (rp < 0.0f) rp += (float) preLen;
                int i0 = (int) rp;
                const float fr = rp - (float) i0;
                if (i0 >= preLen) i0 -= preLen;      // rounding can land on preLen exactly
                const int i1 = i0 + 1 >= preLen ? 0 : i0 + 1;
                const float p0 = pre[0][(size_t) i0] + fr * (pre[0][(size_t) i1] - pre[0][(size_t) i0]);
                const float p1 = pre[1][(size_t) i0] + fr * (pre[1][(size_t) i1] - pre[1][(size_t) i0]);
                if (++prePos >= preLen) prePos = 0;
                reverb.tick (p0, p1, r0, r1);
            }

            // --- TONE (wet low-pass), WIDTH ----------------------------------------
            toneY[0] = snap (toneY[0] + cTone * (r0 - toneY[0]));
            toneY[1] = snap (toneY[1] + cTone * (r1 - toneY[1]));
            float w0 = toneY[0], w1 = toneY[1];
            {
                const float width = widthRamp.next();
                const float m = 0.5f * (w0 + w1), sd = 0.5f * (w0 - w1) * width;
                w0 = m + sd;
                w1 = m - sd;
            }

            // --- return gate: duck on a hit, reopen after AFTER, close by GATE ------
            float tgt, c;
            switch (retPhase)
            {
                case RetPhase::protect:
                    tgt = 0.0f; c = cDuck;
                    if (++retCount >= protectLen) { retPhase = RetPhase::open; retCount = 0; }
                    break;
                case RetPhase::open:
                    tgt = 1.0f; c = cOpen;
                    if (gateOn && ++retCount >= holdLen) retPhase = RetPhase::closing;
                    break;
                case RetPhase::closing:
                default:
                    tgt = 0.0f; c = cClose;
                    //  Live GATE: turning it off, or lengthening the window past
                    //  where the close began, reopens smoothly.
                    if (! gateOn) { retPhase = RetPhase::open; }
                    else if (++retCount < holdLen) { retPhase = RetPhase::open; }
                    break;
            }
            retG1 += c * (tgt - retG1);
            retG2 += c * (retG1 - retG2);
            if (retG2 < 1.0e-7f && tgt == 0.0f) { retG1 = retG2 = 0.0f; }

            const float wg = wetRamp.next() * retG2;
            float o0 = d0, o1 = d1;
            float we0 = 0.0f, we1 = 0.0f;
            if (wg != 0.0f)
            {
                we0 = wg * w0;
                we1 = wg * w1;
                if (stereo) { o0 += we0; o1 += we1; }
                else        { we0 = 0.5f * (we0 + we1); we1 = we0; o0 += we0; }
            }

            // --- OUTPUT, bypass --------------------------------------------------------
            const float og = outRamp.next();
            if (og != 1.0f) { o0 *= og; o1 *= og; }
            const float bm = bypassRamp.next();
            if (bm >= 1.0f)     { o0 = x0; o1 = x1; }
            else if (bm > 0.0f) { o0 += bm * (x0 - o0); o1 += bm * (x1 - o1); }

            c0[n] = o0;
            if (stereo) c1[n] = o1;

            // --- telemetry -----------------------------------------------------------
            const float ad = std::max (std::abs (x0), std::abs (x1));
            const float aw = std::max (std::abs (we0), std::abs (we1));
            mDryL = std::max (mDryL, std::abs (x0)); mDryR = std::max (mDryR, std::abs (x1));
            mWetL = std::max (mWetL, std::abs (we0)); mWetR = std::max (mWetR, std::abs (we1));
            mOutL = std::max (mOutL, std::abs (o0)); mOutR = std::max (mOutR, std::abs (stereo ? o1 : o0));

            preAcc = std::max (preAcc, ad);
            if (++preBinCount >= preBinLen)
            {
                preRing[(size_t) preRingPos] = preAcc;
                if (++preRingPos >= timeAxis::kPreBins) preRingPos = 0;
                preAcc = 0.0f; preBinCount = 0;
            }
            if (capActive)
            {
                capDry = std::max (capDry, ad);
                capWet = std::max (capWet, aw);
                if (++capSample >= binEnd[(size_t) (capBin + 1)])
                {
                    const auto k = (size_t) (timeAxis::kPreBins + capBin);
                    telemetry.dryPeak[k].store (capDry, std::memory_order_relaxed);
                    telemetry.wetPeak[k].store (capWet, std::memory_order_relaxed);
                    telemetry.filled.store (timeAxis::kPreBins + capBin + 1, std::memory_order_release);
                    capDry = capWet = 0.0f;
                    if (++capBin >= timeAxis::kPostBins)
                        capActive = false;
                }
            }
        }

        Telemetry::raise (telemetry.dryL, mDryL); Telemetry::raise (telemetry.dryR, mDryR);
        Telemetry::raise (telemetry.wetL, mWetL); Telemetry::raise (telemetry.wetR, mWetR);
        Telemetry::raise (telemetry.outL, mOutL); Telemetry::raise (telemetry.outR, mOutR);
        if (mOutL >= 1.0f || mOutR >= 1.0f)
            telemetry.clipped.store (true, std::memory_order_relaxed);
    }
}
