// What the audio thread tells the editor, lock-free and allocation-free.
//
// The audio thread is the only writer. The editor polls at ~30 Hz. Every field
// is an atomic, so a torn read is at worst one stale bin in a 30 Hz picture -
// never undefined behaviour, never a lock.
//
// The capture is the timing strip's data: each detected hit starts a new
// capture, holding 20 ms of pre-roll and ~6.2 s after the onset in bins laid
// out on the strip's own warped time axis (timeAxis below), so bin k is drawn
// at x = k / kPostBins across the post-onset region without any resampling.

#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ah
{
    namespace timeAxis
    {
        inline constexpr int   kPreBins  = 24;
        inline constexpr int   kPostBins = 256;
        inline constexpr float kPreMs    = 20.0f;
        inline constexpr float kPostMs   = 6200.0f;
        inline constexpr float kWarpMs   = 30.0f;

        //  Post-onset time (ms) -> 0..1 across the post region. Logarithmic
        //  past ~30 ms, so a 25 ms AFTER gap and a 6 s tail are both legible.
        inline float toUnit (float ms)
        {
            if (ms <= 0.0f) return 0.0f;
            return std::log1p (ms / kWarpMs) / std::log1p (kPostMs / kWarpMs);
        }
        inline float fromUnit (float u)
        {
            return kWarpMs * std::expm1 (u * std::log1p (kPostMs / kWarpMs));
        }
    }

    struct Telemetry
    {
        static constexpr int kBins = timeAxis::kPreBins + timeAxis::kPostBins;

        //  Latest capture. `eventId` changes on every onset; `filled` counts
        //  valid bins (pre-roll bins first).
        std::array<std::atomic<float>, kBins> dryPeak {};
        std::array<std::atomic<float>, kBins> wetPeak {};
        std::atomic<int> filled { 0 };
        std::atomic<std::uint32_t> eventId { 0 };

        //  Meters: running maxima since the editor last took them.
        std::atomic<float> dryL { 0.0f }, dryR { 0.0f }, wetL { 0.0f }, wetR { 0.0f };
        std::atomic<float> outL { 0.0f }, outR { 0.0f };
        std::atomic<bool>  clipped { false };      // latched until the editor clears it

        //  The effective state the DSP is using right now.
        std::atomic<float> windowMs { 0.0f };      // GATE window, 0 = off
        std::atomic<float> afterMs { 25.0f };
        std::atomic<double> bpm { 120.0 };
        std::atomic<bool>  bpmFromHost { false };
        std::atomic<int>   onsets { 0 };

        static void raise (std::atomic<float>& a, float v) noexcept
        {
            if (v > a.load (std::memory_order_relaxed))
                a.store (v, std::memory_order_relaxed);
        }
    };
}
