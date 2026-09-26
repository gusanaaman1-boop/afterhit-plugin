// AFTERHIT's reverb: an input diffuser, an early-reflection tap line and two
// 16-line feedback delay networks ("tanks"). Two tanks so a new hit can land
// in a clean tank while the previous tail is choked smoothly in the other -
// nothing is ever cleared abruptly, and a roll cannot pile up a cloud.
//
// All memory is reserved in prepare(); nothing here allocates afterwards.

#pragma once

#include <array>
#include <vector>

namespace ah
{
    //  Schroeder all-pass, used in series to smear the send before the tanks.
    class Allpass
    {
    public:
        void prepare (int lengthSamples);
        void clear();
        float tick (float x, float g) noexcept
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            if (++pos >= len) pos = 0;
            return d - g * v;
        }
    private:
        std::vector<float> buf;
        int len = 1, pos = 0;
    };

    class FdnTank
    {
    public:
        static constexpr int kLines = 16;

        void prepare (double sampleRate);
        void clear();

        //  Called once per sub-block. T60 in seconds applies below the damping
        //  crossover; above it the tank decays in hfRatio * T60.
        void setDecay (float t60Seconds, float hfRatio) noexcept;

        //  One stereo sample in, one stereo sample out.
        void tick (float inL, float inR, float& outL, float& outR) noexcept;

        //  Largest line output since the last call, then reset. Used to put an
        //  inaudible tank to sleep.
        float takePeak() noexcept { const float p = peak; peak = 0.0f; return p; }

        bool isAsleep() const noexcept { return asleep; }
        void sleep();                       // zero the lines (inaudible by then)
        void wake() noexcept { asleep = false; }

    private:
        struct Line
        {
            std::vector<float> buf;
            int len = 1, pos = 0;
            float gainLo = 0.0f, gainHi = 0.0f, lp = 0.0f;
        };
        std::array<Line, kLines> lines;
        std::array<float, kLines> v {};

        double fs = 48000.0;
        float xover = 0.3f, peak = 0.0f;
        bool asleep = true;

        //  Slow modulation of two lines' read points breaks up metallic modes
        //  in long tails.
        float modPhaseA = 0.0f, modPhaseB = 0.25f, modIncA = 0.0f, modIncB = 0.0f, modDepth = 0.0f;
        int modLineA = 3, modLineB = 10;
    };

    //  The whole wet generator: send in, raw stereo reverb out (before tone,
    //  width and the return gate, which the engine owns).
    class Reverb
    {
    public:
        void prepare (double sampleRate);
        void clear();

        void setDecay (float t60Seconds) noexcept { tailT60 = t60Seconds; }
        void setHfDecayRatio (float r) noexcept { hfRatio = r; }

        //  A new hit was detected. With choking on (GATE > 0) the tank holding
        //  the old tail starts decaying at the choke T60 now, and the input
        //  moves to the other tank after `routeDelaySamples` - the moment the
        //  new hit comes out of the pre-delay. With choking off, both tanks
        //  ring at the natural TAIL.
        void beginEvent (bool choke, int routeDelaySamples) noexcept;

        //  Once per sub-block: move the smoothed T60s along.
        void updateCoefficients() noexcept;

        void tick (float inL, float inR, float& outL, float& outR) noexcept;

        //  Once per sub-block: put inaudible tanks with no input to sleep.
        void housekeep() noexcept;

        int activeTank() const noexcept { return active; }
        bool isTankAsleep (int t) const noexcept { return tanks[(size_t) t].isAsleep(); }

    private:
        double fs = 48000.0;
        std::array<FdnTank, 2> tanks;
        std::array<Allpass, 4> diffL, diffR;

        //  Early reflections: taps from a short line on the diffused send.
        std::vector<float> erL, erR;
        int erLen = 1, erPos = 0;
        std::array<int, 6> erTapL {}, erTapR {};

        int active = 0, pendingRoute = -1;
        float tailT60 = 1.0f, hfRatio = 0.5f;
        std::array<float, 2> hfNow { 0.5f, 0.5f };
        std::array<float, 2> t60Now { 1.0f, 1.0f };      // smoothed, per tank
        std::array<bool, 2> choked { false, false };
        std::array<float, 2> tankInPeak { 0.0f, 0.0f };
        float t60Coef = 0.0f;

        //  Crossfade of the input between tanks (0 = tank 0, 1 = tank 1).
        float route = 0.0f, routeStep = 0.0f;
    };
}
