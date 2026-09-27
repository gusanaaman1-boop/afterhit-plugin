#include "Reverb.h"

#include <algorithm>
#include <cmath>

#include "Laws.h"

namespace ah
{
    namespace
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        //  Sixteen line lengths, geometric from 19 to 58 ms. Long enough for a
        //  real room, short enough that a 0.15 s tail still builds density
        //  instead of fluttering.
        constexpr float kLineMs[FdnTank::kLines] = {
            19.0f, 20.5f, 22.1f, 23.7f, 25.6f, 27.5f, 29.7f, 31.9f,
            34.4f, 37.1f, 39.9f, 43.0f, 46.4f, 49.9f, 53.8f, 57.9f };

        constexpr float kDiffMsL[4] = { 1.3f, 2.9f, 4.1f, 6.7f };
        constexpr float kDiffMsR[4] = { 1.7f, 3.1f, 4.7f, 7.3f };
        constexpr float kDiffGain   = 0.6f;

        constexpr float kErMsL[6]  = { 3.1f, 5.3f, 7.9f, 10.7f, 13.9f, 17.3f };
        constexpr float kErMsR[6]  = { 3.7f, 6.1f, 8.9f, 11.9f, 15.1f, 18.7f };
        constexpr float kErGain[6] = { 0.62f, -0.50f, 0.42f, -0.34f, 0.27f, -0.21f };
        constexpr float kErLevel   = 0.45f;

        //  Injection into the tank and the output tap scale. The tank's own
        //  loudness is calibrated once in the engine (kReverbNorm).
        constexpr float kInGain  = 0.35f;
        constexpr float kOutGain = 0.35355339f;   // 1 / sqrt (8)

        constexpr float kSleepPeak  = 1.0e-7f;    // -140 dBFS
        constexpr float kWakeInput  = 1.0e-9f;

        int msToSamples (double fs, float ms) { return std::max (1, (int) std::lround (ms * 0.001 * fs)); }

        //  In-place fast Walsh-Hadamard transform of 16 values, scaled to be
        //  orthogonal (1/4). Energy-preserving, so the loop is stable for every
        //  line gain < 1.
        inline void hadamard16 (std::array<float, FdnTank::kLines>& v) noexcept
        {
            for (int h = 1; h < FdnTank::kLines; h <<= 1)
                for (int i = 0; i < FdnTank::kLines; i += h << 1)
                    for (int j = i; j < i + h; ++j)
                    {
                        const float a = v[(size_t) j], b = v[(size_t) (j + h)];
                        v[(size_t) j] = a + b;
                        v[(size_t) (j + h)] = a - b;
                    }
            for (auto& x : v)
                x *= 0.25f;
        }
    }

    // --- Allpass ----------------------------------------------------------------
    void Allpass::prepare (int lengthSamples)
    {
        len = std::max (1, lengthSamples);
        buf.assign ((size_t) len, 0.0f);
        pos = 0;
    }

    void Allpass::clear()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        pos = 0;
    }

    // --- FdnTank ----------------------------------------------------------------
    void FdnTank::prepare (double sampleRate)
    {
        fs = sampleRate;
        modDepth = (float) (0.00025 * fs);                // 0.25 ms
        const int margin = (int) std::ceil (modDepth) + 4;
        for (int i = 0; i < kLines; ++i)
        {
            auto& l = lines[(size_t) i];
            l.len = msToSamples (fs, kLineMs[i]) | 1;
            l.buf.assign ((size_t) (l.len + margin), 0.0f);
        }
        modIncA = (float) (0.71 / fs);
        modIncB = (float) (0.93 / fs);
        xover = (float) (1.0 - std::exp (-kTwoPi * 3000.0 / fs));
        clear();
        setDecay (1.0f, 0.5f);
    }

    void FdnTank::clear()
    {
        for (auto& l : lines)
        {
            std::fill (l.buf.begin(), l.buf.end(), 0.0f);
            l.pos = 0;
            l.lp = 0.0f;
        }
        v.fill (0.0f);
        peak = 0.0f;
        asleep = true;
    }

    void FdnTank::sleep()
    {
        clear();
    }

    void FdnTank::setDecay (float t60Seconds, float hfRatio) noexcept
    {
        const double t60 = std::max (0.01, (double) t60Seconds);
        const double t60Hi = std::max (0.01, t60 * (double) hfRatio);
        for (auto& l : lines)
        {
            l.gainLo = (float) std::pow (10.0, -3.0 * (double) l.len / (fs * t60));
            l.gainHi = (float) std::pow (10.0, -3.0 * (double) l.len / (fs * t60Hi));
        }
    }

    void FdnTank::tick (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (asleep)
        {
            if (std::abs (inL) <= kWakeInput && std::abs (inR) <= kWakeInput)
            {
                outL = outR = 0.0f;
                return;
            }
            asleep = false;
        }

        modPhaseA += modIncA; if (modPhaseA >= 1.0f) modPhaseA -= 1.0f;
        modPhaseB += modIncB; if (modPhaseB >= 1.0f) modPhaseB -= 1.0f;

        float l = 0.0f, r = 0.0f, pk = peak;
        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[(size_t) i];
            const int size = (int) line.buf.size();
            float o;
            if (i == modLineA || i == modLineB)
            {
                const float ph = i == modLineA ? modPhaseA : modPhaseB;
                const float d = (float) line.len - modDepth * 0.5f * (1.0f + std::sin (kTwoPi * ph));
                float rp = (float) line.pos - d;
                while (rp < 0.0f) rp += (float) size;
                //  Float rounding can land rp on exactly `size` (pos - d a hair
                //  below zero, + size): wrap the index, never read past the end.
                int i0 = (int) rp;
                const float fr = rp - (float) i0;
                if (i0 >= size) i0 -= size;
                const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
                o = line.buf[(size_t) i0] + fr * (line.buf[(size_t) i1] - line.buf[(size_t) i0]);
            }
            else
            {
                int rp = line.pos - line.len;
                if (rp < 0) rp += size;
                o = line.buf[(size_t) rp];
            }
            pk = std::max (pk, std::abs (o));
            //  Two-band decay: below the crossover the line loses exactly what
            //  TAIL asks for; above it, what TONE asks for.
            line.lp += xover * (o - line.lp);
            if (std::abs (line.lp) < 1.0e-20f) line.lp = 0.0f;
            const float y = o;
            //  Taps: even lines to the left, odd to the right, alternating sign
            //  so the two sides are decorrelated.
            if ((i & 1) == 0) l += ((i & 2) == 0 ? y : -y);
            else              r += ((i & 2) == 0 ? y : -y);
            v[(size_t) i] = line.gainLo * line.lp + line.gainHi * (o - line.lp);
        }
        peak = pk;

        hadamard16 (v);

        for (int i = 0; i < kLines; ++i)
        {
            auto& line = lines[(size_t) i];
            const float in = (i & 1) == 0 ? inL : inR;
            line.buf[(size_t) line.pos] = v[(size_t) i] + kInGain * in;
            if (++line.pos >= (int) line.buf.size())
                line.pos = 0;
        }

        outL = l * kOutGain;
        outR = r * kOutGain;
    }

    // --- Reverb -----------------------------------------------------------------
    void Reverb::prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& t : tanks)
            t.prepare (fs);
        for (int k = 0; k < 4; ++k)
        {
            diffL[(size_t) k].prepare (msToSamples (fs, kDiffMsL[k]));
            diffR[(size_t) k].prepare (msToSamples (fs, kDiffMsR[k]));
        }
        int longest = 1;
        for (int k = 0; k < 6; ++k)
        {
            erTapL[(size_t) k] = msToSamples (fs, kErMsL[k]);
            erTapR[(size_t) k] = msToSamples (fs, kErMsR[k]);
            longest = std::max ({ longest, erTapL[(size_t) k], erTapR[(size_t) k] });
        }
        erLen = longest + 1;
        erL.assign ((size_t) erLen, 0.0f);
        erR.assign ((size_t) erLen, 0.0f);

        //  T60 smoothing: ~3 ms time constant, applied per 32-sample sub-block.
        t60Coef = (float) (1.0 - std::exp (-32.0 / (0.003 * fs)));
        clear();
    }

    void Reverb::clear()
    {
        for (auto& t : tanks)
            t.clear();
        for (auto& a : diffL) a.clear();
        for (auto& a : diffR) a.clear();
        std::fill (erL.begin(), erL.end(), 0.0f);
        std::fill (erR.begin(), erR.end(), 0.0f);
        erPos = 0;
        active = 0;
        route = 0.0f;
        routeStep = 0.0f;
        pendingRoute = -1;
        choked = { false, false };
        t60Now = { tailT60, tailT60 };
        hfNow = { hfRatio, hfRatio };
        tankInPeak = { 0.0f, 0.0f };
        for (size_t t = 0; t < 2; ++t)
            tanks[t].setDecay (t60Now[t], hfNow[t]);
    }

    void Reverb::beginEvent (bool choke, int routeDelaySamples) noexcept
    {
        if (! choke)
        {
            //  GATE off: one tank, natural decay. Anything choked earlier is
            //  released so the next tail rings out fully.
            choked = { false, false };
            return;
        }
        if (pendingRoute >= 0)
            return;     // the previous event's switch has not happened yet - share it
        choked[(size_t) active] = true;
        pendingRoute = std::max (0, routeDelaySamples);
    }

    void Reverb::updateCoefficients() noexcept
    {
        for (size_t t = 0; t < 2; ++t)
        {
            const float target = choked[t] ? std::min (law::kChokeT60, tailT60) : tailT60;
            const float next = std::exp (std::log (t60Now[t]) + t60Coef * (std::log (target) - std::log (t60Now[t])));
            const float hf = hfNow[t] + t60Coef * (hfRatio - hfNow[t]);
            if (next != t60Now[t] || hf != hfNow[t])
            {
                t60Now[t] = next;
                hfNow[t] = hf;
                tanks[t].setDecay (next, hf);
            }
        }
    }

    void Reverb::tick (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (pendingRoute >= 0 && pendingRoute-- == 0)
        {
            //  The new hit reaches the tanks now: move the input to the other
            //  tank (2 ms crossfade) and let it ring at the full TAIL.
            const int next = 1 - active;
            choked[(size_t) next] = false;
            active = next;
            routeStep = (float) ((next == 1 ? 1.0 : -1.0) / (0.002 * fs));
        }
        if (routeStep != 0.0f)
        {
            route += routeStep;
            if (route >= 1.0f) { route = 1.0f; routeStep = 0.0f; }
            if (route <= 0.0f) { route = 0.0f; routeStep = 0.0f; }
        }

        float dl = inL, dr = inR;
        for (size_t k = 0; k < 4; ++k)
        {
            dl = diffL[k].tick (dl, kDiffGain);
            dr = diffR[k].tick (dr, kDiffGain);
        }

        erL[(size_t) erPos] = dl;
        erR[(size_t) erPos] = dr;
        float eL = 0.0f, eR = 0.0f;
        for (size_t k = 0; k < 6; ++k)
        {
            int pl = erPos - erTapL[k]; if (pl < 0) pl += erLen;
            int pr = erPos - erTapR[k]; if (pr < 0) pr += erLen;
            //  Each side hears mostly its own source and a little of the other.
            eL += kErGain[k] * (erL[(size_t) pl] * 0.8f + erR[(size_t) pl] * 0.2f);
            eR += kErGain[k] * (erR[(size_t) pr] * 0.8f + erL[(size_t) pr] * 0.2f);
        }
        if (++erPos >= erLen) erPos = 0;

        const float w1 = route, w0 = 1.0f - route;
        float aL, aR, bL, bR;
        const float in0L = dl * w0, in0R = dr * w0, in1L = dl * w1, in1R = dr * w1;
        tankInPeak[0] = std::max ({ tankInPeak[0], std::abs (in0L), std::abs (in0R) });
        tankInPeak[1] = std::max ({ tankInPeak[1], std::abs (in1L), std::abs (in1R) });
        tanks[0].tick (in0L, in0R, aL, aR);
        tanks[1].tick (in1L, in1R, bL, bR);

        outL = aL + bL + kErLevel * eL;
        outR = aR + bR + kErLevel * eR;
    }

    void Reverb::housekeep() noexcept
    {
        for (size_t t = 0; t < 2; ++t)
        {
            const float p = tanks[t].takePeak();
            if (! tanks[t].isAsleep() && p < kSleepPeak && tankInPeak[t] < kWakeInput)
                tanks[t].sleep();
            tankInPeak[t] = 0.0f;
        }
    }
}
