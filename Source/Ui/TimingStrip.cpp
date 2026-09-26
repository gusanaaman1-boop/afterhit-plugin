#include "TimingStrip.h"

#include "Theme.h"
#include "../Core/ParameterIds.h"
#include "../Core/Parameters.h"
#include "../Dsp/Laws.h"

namespace ah::ui
{
    namespace
    {
        //  Wet peak / dry peak the reverb produces for a clap-like hit at a
        //  wet gain of 1 (measured by AfterHitTests, "calibration" section).
        //  Only the MODEL layer uses it; the capture layer is measured audio.
        constexpr float kModelWetRatio = 0.234f;
        constexpr float kModelHitDb    = -6.0f;     // the reference hit
        constexpr float kFloorDb = -54.0f, kSpanDb = 48.0f;

        constexpr float kPreFraction = 0.10f;       // of the plot width

        float dbOf (float a) { return a > 1.0e-6f ? 20.0f * std::log10 (a) : -120.0f; }
    }

    TimingStrip::TimingStrip (juce::AudioProcessorValueTreeState& s, Telemetry& t)
        : state (s), telemetry (t)
    {
        setInterceptsMouseClicks (false, false);
        setOpaque (true);
    }

    juce::Rectangle<float> TimingStrip::plotArea() const
    {
        return getLocalBounds().toFloat().reduced ((float) metric::gutter, 6.0f);
    }

    float TimingStrip::xForMs (float ms) const noexcept
    {
        const auto a = plotArea();
        const float preW = a.getWidth() * kPreFraction;
        const float x0 = a.getX() + preW;
        if (ms < 0.0f)
            return x0 + juce::jmax (-1.0f, ms / timeAxis::kPreMs) * preW;
        return x0 + timeAxis::toUnit (ms) * (a.getRight() - x0);
    }

    TimingStrip::Model TimingStrip::readModel() const
    {
        auto get = [this] (const char* pid) { return state.getRawParameterValue (pid)->load(); };
        Model m;
        m.hit = get (id::hit);
        m.space = get (id::space);
        m.tail = get (id::tail);
        m.gate = get (id::gate);
        m.after = get (id::after);
        m.sync = get (id::sync) > 0.5f;
        m.windowMs = law::gateWindowMs (m.gate, m.sync, telemetry.bpm.load (std::memory_order_relaxed));
        return m;
    }

    float TimingStrip::modelWetDb (const Model& m, float ms) const
    {
        const float t = ms - m.after;
        if (t <= 0.0f || m.space <= 0.0f)
            return -120.0f;
        const float gain = law::spaceGain (m.space) * law::tailCompensation (m.tail) * kModelWetRatio;
        float db = kModelHitDb + dbOf (gain);
        db += dbOf (1.0f - std::exp (-t / 5.0f));                  // bloom
        db -= 60.0f * t / (m.tail * 1000.0f);                      // TAIL
        if (m.windowMs > 0.0f)
        {
            const float hold = law::gateHoldMs (m.windowMs);
            if (t > hold)
            {
                const float x = (t - hold) / (law::gateFadeMs (m.windowMs) / 5.0f);
                db += dbOf ((1.0f + x) * std::exp (-x));           // GATE close
            }
        }
        return db;
    }

    void TimingStrip::refresh()
    {
        bool changed = false;

        const auto id = telemetry.eventId.load (std::memory_order_acquire);
        const int f = telemetry.filled.load (std::memory_order_acquire);
        if (id != shownEventId || f != filled)
        {
            const int from = id != shownEventId ? 0 : filled;
            for (int k = from; k < f; ++k)
            {
                dry[(size_t) k] = telemetry.dryPeak[(size_t) k].load (std::memory_order_relaxed);
                wet[(size_t) k] = telemetry.wetPeak[(size_t) k].load (std::memory_order_relaxed);
            }
            shownEventId = id;
            filled = f;
            changed = true;
        }

        const auto m = readModel();
        if (m.hit != lastModel.hit || m.space != lastModel.space || m.tail != lastModel.tail || m.gate != lastModel.gate
            || m.after != lastModel.after || m.windowMs != lastModel.windowMs || m.sync != lastModel.sync)
        {
            lastModel = m;
            changed = true;
        }

        if (changed)
            repaint();
    }

    void TimingStrip::paint (juce::Graphics& g)
    {
        g.fillAll (colour::surface);

        const auto a = plotArea();
        const auto m = lastModel;
        const float base = a.getCentreY() + 6.0f;
        const float up = base - a.getY() - 2.0f;       // room above the baseline
        const float half = juce::jmin (up, a.getBottom() - base + 12.0f);

        const bool haveCapture = shownEventId != 0 && filled > timeAxis::kPreBins;

        //  Protected gap: onset -> AFTER.
        const float xOn = xForMs (0.0f), xAfter = xForMs (m.after);
        if (xAfter - xOn > 0.5f)
        {
            g.setColour (colour::protect);
            g.fillRect (juce::Rectangle<float> (xOn, a.getY() + 10.0f, xAfter - xOn, a.getHeight() - 14.0f));
        }

        //  Baseline.
        g.setColour (colour::hairline);
        g.fillRect (juce::Rectangle<float> (a.getX(), base - 0.5f, a.getWidth(), 1.0f));

        //  "HIT" tag.
        g.setColour (colour::secondary);
        g.setFont (font (10.0f, false, 0.14f));
        g.drawText ("HIT", juce::Rectangle<float> (a.getX(), a.getY() - 2.0f, 40.0f, 12.0f), juce::Justification::centredLeft, false);

        // --- wet: model and (if any) capture -------------------------------------
        auto heightForDb = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / kSpanDb) * (up - 4.0f); };

        auto buildWet = [&] (auto&& dbAt) -> juce::Path
        {
            juce::Path p;
            p.startNewSubPath (xAfter, base);
            for (int k = 0; k < timeAxis::kPostBins; ++k)
            {
                const float u = ((float) k + 0.5f) / (float) timeAxis::kPostBins;
                const float ms = timeAxis::fromUnit (u);
                if (ms < m.after) continue;
                p.lineTo (xForMs (ms), base - heightForDb (dbAt (k, ms)));
            }
            p.lineTo (a.getRight(), base);
            return p;
        };

        auto fillAndStroke = [&] (const juce::Path& p, float alpha)
        {
            juce::Path area (p);
            area.closeSubPath();
            g.setGradientFill (juce::ColourGradient (colour::wet.withAlpha (0.30f * alpha), 0.0f, a.getY(),
                                                     colour::wet.withAlpha (0.02f), 0.0f, base, false));
            g.fillPath (area);
            g.setColour (colour::wet.withAlpha (alpha));
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };

        const auto modelPath = buildWet ([&] (int, float ms) { return modelWetDb (m, ms); });
        if (! haveCapture)
        {
            fillAndStroke (modelPath, 1.0f);
        }
        else
        {
            const auto real = buildWet ([&] (int k, float) {
                const int i = timeAxis::kPreBins + k;
                return i < filled ? dbOf (wet[(size_t) i]) : -120.0f; });
            fillAndStroke (real, 1.0f);
            //  The current settings' model, faint, so a knob move shows at once.
            g.setColour (colour::wet.withAlpha (0.35f));
            g.strokePath (modelPath, juce::PathStrokeType (1.0f));
        }

        // --- dry hit -------------------------------------------------------------------
        if (haveCapture)
        {
            for (int i = 0; i < filled; ++i)
            {
                float x0, x1;
                if (i < timeAxis::kPreBins)
                {
                    x0 = xForMs (-timeAxis::kPreMs + timeAxis::kPreMs * (float) i / timeAxis::kPreBins);
                    x1 = xForMs (-timeAxis::kPreMs + timeAxis::kPreMs * (float) (i + 1) / timeAxis::kPreBins);
                }
                else
                {
                    const int k = i - timeAxis::kPreBins;
                    x0 = xForMs (timeAxis::fromUnit ((float) k / timeAxis::kPostBins));
                    x1 = xForMs (timeAxis::fromUnit ((float) (k + 1) / timeAxis::kPostBins));
                }
                const float h = half * 0.92f * juce::jmin (1.0f, std::sqrt (dry[(size_t) i] / 0.9f));
                if (h < 0.5f) continue;
                const float xc = 0.5f * (x0 + x1);
                const bool protectedPart = xc <= xAfter + 0.5f;
                g.setColour (colour::dry.withAlpha (protectedPart ? 0.95f : 0.18f));
                g.fillRect (juce::Rectangle<float> (xc - juce::jmax (0.6f, 0.5f * (x1 - x0) - 0.3f), base - h,
                                                    juce::jmax (1.2f, x1 - x0 - 0.6f), 2.0f * h));
            }
        }
        else
        {
            //  Reference hit for the model: a short decaying transient.
            juce::Path p;
            p.startNewSubPath (a.getX(), base);
            p.lineTo (xForMs (-4.0f), base);
            const float end = juce::jmax (6.0f, juce::jmin (m.after, 24.0f));
            int k = 0;
            for (float t = 0.0f; t <= end; t += 1.2f, ++k)
            {
                const float amp = half * 0.9f * std::exp (-t / 6.5f) * (0.35f + 0.65f * (float) ((k * 7) % 5) / 4.0f);
                p.lineTo (xForMs (t), base + ((k & 1) ? amp : -amp));
            }
            p.lineTo (xForMs (end + 1.0f), base);
            g.setColour (colour::dry.withAlpha (0.9f));
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered));
        }

        // --- markers --------------------------------------------------------------------
        g.setColour (colour::wet);
        g.fillRect (juce::Rectangle<float> (xAfter - 0.75f, base - 18.0f, 1.5f, 30.0f));
        g.setFont (numberFont (11.0f));
        g.drawText ("AFTER " + formatMs (m.after), juce::Rectangle<float> (xAfter + 3.0f, a.getY() - 2.0f, 120.0f, 13.0f),
                    juce::Justification::centredLeft, false);

        const bool gateOn = m.windowMs > 0.0f;
        const float endMs = m.after + (gateOn ? m.windowMs : m.tail * 1000.0f);
        const float xEnd = xForMs (endMs);
        g.fillRect (juce::Rectangle<float> (xEnd - 0.75f, base - 9.0f, 1.5f, 18.0f));
        g.setColour (colour::secondary);
        g.setFont (numberFont (10.5f));
        const auto endText = gateOn ? "GATE " + formatMs (m.windowMs) : "TAIL " + formatTail (m.tail);
        const bool roomRight = xEnd + 80.0f < a.getRight();
        g.drawText (endText, juce::Rectangle<float> (roomRight ? xEnd + 4.0f : xEnd - 84.0f, base + 2.0f, 80.0f, 12.0f),
                    roomRight ? juce::Justification::centredLeft : juce::Justification::centredRight, false);
    }
}
