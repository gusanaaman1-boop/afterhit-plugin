// AFTERHIT DSP measurement suite.
//
//     build/AfterHitTests_artefacts/<config>/AfterHitTests
//
// Drives AfterHitEngine directly with synthetic drum hits and asserts the
// spec's section-7 behaviour as numbers. Exit 0 = every check passed.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Dsp/AfterHitEngine.h"

using ah::AfterHitEngine;
namespace law = ah::law;

namespace
{
    int gChecks = 0, gFailures = 0;

    void check (bool ok, const char* label, const std::string& measured = {})
    {
        ++gChecks;
        if (! ok) ++gFailures;
        std::printf ("  [%s] %-66s %s\n", ok ? "PASS" : "FAIL", label, measured.c_str());
    }
    void section (const char* name) { std::printf ("\n== %s ==\n", name); }
    std::string fmt (const char* f, double a) { char b[64]; std::snprintf (b, sizeof b, f, a); return b; }
    std::string fmt2 (const char* f, double a, double c) { char b[96]; std::snprintf (b, sizeof b, f, a, c); return b; }
    double db (double a) { return a > 1e-12 ? 20.0 * std::log10 (a) : -240.0; }

    struct Stereo
    {
        std::vector<float> l, r;
        explicit Stereo (size_t n = 0) : l (n, 0.0f), r (n, 0.0f) {}
        size_t size() const { return l.size(); }
    };

    // --- deterministic sources -----------------------------------------------------
    struct Rng
    {
        juce::uint32 s;
        float next() { s = s * 1664525u + 1013904223u; return (float) ((double) (s >> 8) / 8388608.0 - 1.0); }
    };

    //  A clap: three short noise bursts 8 ms apart, then a 60 ms noise body.
    void addClap (Stereo& b, double fs, size_t at, float peak = 0.5f, juce::uint32 seed = 7, bool left = true, bool right = true)
    {
        Rng rng { seed };
        const int n = (int) (0.12 * fs);
        for (int i = 0; i < n && at + (size_t) i < b.size(); ++i)
        {
            const double t = (double) i / fs;
            double env = 0.0;
            for (int k = 0; k < 3; ++k)
            {
                const double tk = t - 0.008 * k;
                if (tk >= 0.0) env = std::max (env, std::exp (-tk / 0.0018));
            }
            if (t >= 0.016) env = std::max (env, 0.55 * std::exp (-(t - 0.016) / 0.025));
            const float v = peak * (float) env * rng.next();
            if (left)  b.l[at + (size_t) i] += v;
            if (right) b.r[at + (size_t) i] += v * 0.9f;
        }
    }

    //  A kick: 150 -> 48 Hz sweep with a 250 ms body and a click.
    void addKick (Stereo& b, double fs, size_t at, float peak = 0.9f)
    {
        const int n = (int) (0.6 * fs);
        double ph = 0.0;
        for (int i = 0; i < n && at + (size_t) i < b.size(); ++i)
        {
            const double t = (double) i / fs;
            const double f = 48.0 + 102.0 * std::exp (-t / 0.03);
            ph += 2.0 * juce::MathConstants<double>::pi * f / fs;
            const double v = std::sin (ph) * std::exp (-t / 0.25) + (i < (int) (0.002 * fs) ? 0.3 * (1.0 - t / 0.002) : 0.0);
            b.l[at + (size_t) i] += peak * (float) v * 0.95f;
            b.r[at + (size_t) i] += peak * (float) v * 0.95f;
        }
    }

    //  A snare: 190 Hz tone + noise, 120 ms.
    void addSnare (Stereo& b, double fs, size_t at, float peak = 0.6f, juce::uint32 seed = 3)
    {
        Rng rng { seed };
        const int n = (int) (0.3 * fs);
        for (int i = 0; i < n && at + (size_t) i < b.size(); ++i)
        {
            const double t = (double) i / fs;
            const double v = 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * 190.0 * t) * std::exp (-t / 0.05)
                           + 0.6 * rng.next() * std::exp (-t / 0.07);
            b.l[at + (size_t) i] += peak * (float) v;
            b.r[at + (size_t) i] += peak * (float) v;
        }
    }

    Stereo drumLoop (double fs, double seconds)
    {
        Stereo b ((size_t) (seconds * fs));
        const double beat = 60.0 / 124.0;
        juce::uint32 seed = 11;
        for (int k = 0; (double) k * beat * 0.5 < seconds - 0.7; ++k)
        {
            const auto at = (size_t) ((double) k * beat * 0.5 * fs);
            if (k % 2 == 0) addKick (b, fs, at, 0.8f);
            if (k % 4 == 2) addSnare (b, fs, at, 0.5f, seed++);
            if (k % 8 == 7) addClap (b, fs, at, 0.35f, seed++);
            addClap (b, fs, at + (size_t) (0.25 * beat * fs), 0.06f, seed++);   // quiet percussion
        }
        return b;
    }

    // --- runner ---------------------------------------------------------------------
    struct Run
    {
        AfterHitEngine::Settings s;
        double fs = 48000.0;
        int block = 256;
        bool mono = false;
        std::function<void (AfterHitEngine&, size_t)> perBlock;   // optional hook
    };

    Stereo process (AfterHitEngine& e, const Run& r, const Stereo& in)
    {
        Stereo out = in;
        size_t pos = 0;
        while (pos < in.size())
        {
            const int n = (int) std::min ((size_t) r.block, in.size() - pos);
            if (r.perBlock) r.perBlock (e, pos);
            float* ch[2] = { out.l.data() + pos, out.r.data() + pos };
            e.process (ch, r.mono ? 1 : 2, n);
            pos += (size_t) n;
        }
        if (r.mono) out.r = out.l;
        return out;
    }

    Stereo run (const Run& r, const Stereo& in, AfterHitEngine* keep = nullptr)
    {
        AfterHitEngine local;
        AfterHitEngine& e = keep ? *keep : local;
        e.setSettings (r.s);
        e.prepare (r.fs, r.block, r.mono ? 1 : 2);
        return process (e, r, in);
    }

    Stereo wetOf (const Stereo& out, const Stereo& in, bool mono = false)
    {
        Stereo w (in.size());
        for (size_t i = 0; i < in.size(); ++i)
        {
            w.l[i] = out.l[i] - in.l[i];
            w.r[i] = mono ? w.l[i] : out.r[i] - in.r[i];
        }
        return w;
    }

    double peakIn (const Stereo& b, size_t from, size_t to)
    {
        double m = 0.0;
        to = std::min (to, b.size());
        for (size_t i = from; i < to; ++i) m = std::max ({ m, (double) std::abs (b.l[i]), (double) std::abs (b.r[i]) });
        return m;
    }
    double rmsIn (const Stereo& b, size_t from, size_t to)
    {
        double s = 0.0; to = std::min (to, b.size());
        if (to <= from) return 0.0;
        for (size_t i = from; i < to; ++i) s += 0.5 * ((double) b.l[i] * b.l[i] + (double) b.r[i] * b.r[i]);
        return std::sqrt (s / (double) (to - from));
    }
    size_t firstAbove (const Stereo& b, double thr, size_t from = 0)
    {
        for (size_t i = from; i < b.size(); ++i)
            if (std::abs (b.l[i]) > thr || std::abs (b.r[i]) > thr) return i;
        return b.size();
    }
    bool finite (const Stereo& b)
    {
        for (size_t i = 0; i < b.size(); ++i) if (! std::isfinite (b.l[i]) || ! std::isfinite (b.r[i])) return false;
        return true;
    }
    bool identical (const Stereo& a, const Stereo& b)
    {
        return a.l == b.l && a.r == b.r;
    }

    //  Time (s after the envelope's peak) at which a 10 ms-RMS envelope of
    //  `w` has fallen `dropDb` below that peak.
    double decayTime (const Stereo& w, double fs, size_t from, double dropDb)
    {
        const size_t win = (size_t) (0.01 * fs);
        std::vector<double> env;
        for (size_t i = from; i + win < w.size(); i += win) env.push_back (rmsIn (w, i, i + win));
        size_t imax = 0;
        for (size_t k = 0; k < env.size(); ++k) if (env[k] > env[imax]) imax = k;
        const double thr = env[imax] * std::pow (10.0, -dropDb / 20.0);
        for (size_t k = imax; k < env.size(); ++k)
            if (env[k] < thr) return (double) ((k - imax) * win) / fs;
        return (double) ((env.size() - imax) * win) / fs;
    }

    AfterHitEngine::Settings neutral()
    {
        AfterHitEngine::Settings s;
        s.hitPct = 0; s.spacePct = 0; s.outDb = 0;
        return s;
    }
}

int main()
{
    std::printf ("AFTERHIT DSP measurement suite\n");
    const double fs = 48000.0;
    auto ms = [] (double m, double rate = 48000.0) { return (size_t) std::llround (m * 0.001 * rate); };

    // 0 -------------------------------------------------------------------------
    section ("0. Calibration (printed, used by the strip's model and the SPACE law)");
    {
        Stereo in ((size_t) (2.5 * fs));
        addClap (in, fs, ms (100));
        Run r; r.s.hitPct = 0; r.s.spacePct = 100; r.s.gatePct = 0; r.s.tailS = 1.0f;
        AfterHitEngine e;
        const auto out = run (r, in, &e);
        const auto w = wetOf (out, in);
        const double gainAt100 = law::spaceGain (100.0f) * law::tailCompensation (1.0f);
        const double ratio = peakIn (w, 0, w.size()) / peakIn (in, 0, in.size()) / gainAt100;
        std::printf ("  wet peak / dry peak at wet gain 1:  %.3f\n", ratio);

        Run d; d.s.hitPct = 0;     // defaults
        const auto od = run (d, in);
        const auto wd = wetOf (od, in);
        std::printf ("  default settings: wet peak %.1f dB under the clap's peak\n",
                     db (peakIn (in, 0, in.size())) - db (peakIn (wd, 0, wd.size())));
        std::printf ("  default settings: wet RMS (post-AFTER 200 ms) %.1f dB under the clap's RMS (first 30 ms)\n",
                     db (rmsIn (in, ms (100), ms (130))) - db (rmsIn (wd, ms (125), ms (325))));
    }

    // 1 -------------------------------------------------------------------------
    section ("1. Neutrality: HIT 0, SPACE 0, OUTPUT 0 is sample-exact");
    {
        auto in = drumLoop (fs, 3.0);
        //  Plus values over full scale: no hidden limiter or clip.
        in.l[ms (2900)] = 2.0f; in.r[ms (2900)] = -1.7f;
        bool allExact = true;
        const float tails[] = { 0.15f, 1.0f, 6.0f }, gates[] = { 0.0f, 40.0f, 100.0f }, afters[] = { 0.0f, 25.0f, 100.0f };
        const int blocks[] = { 1, 7, 64, 480, 4096 };
        int combos = 0;
        for (int bi = 0; bi < 5; ++bi)
            for (int k = 0; k < 3; ++k)
                for (int mono = 0; mono < 2; ++mono)
                {
                    Run r; r.s = neutral(); r.s.tailS = tails[k]; r.s.gatePct = gates[(k + bi) % 3]; r.s.afterMs = afters[(k + 2 * bi) % 3];
                    r.s.widthPct = 150.0f; r.s.tonePct = 0.0f; r.s.sensPct = 100.0f;
                    r.block = blocks[bi]; r.mono = mono == 1;
                    Stereo src = in;
                    if (r.mono) src.r = src.l;
                    const auto out = run (r, src);
                    if (! identical (out, src)) allExact = false;
                    ++combos;
                }
        check (allExact, "output == input bit for bit (mono+stereo, 5 block sizes, TAIL/GATE/AFTER)", std::to_string (combos) + " runs");

        //  Also at 44.1 and 96 kHz.
        bool rates = true;
        for (double rate : { 44100.0, 96000.0 })
        {
            auto src = drumLoop (rate, 2.0);
            Run r; r.s = neutral(); r.fs = rate; r.block = 333;
            if (! identical (run (r, src), src)) rates = false;
        }
        check (rates, "neutral is also exact at 44.1 and 96 kHz");

        //  Coming back to neutral after processing lands exactly (ramps end on target).
        {
            auto src = drumLoop (fs, 3.0);
            Run r; r.s.hitPct = 80; r.s.spacePct = 0; r.s.outDb = 0; r.block = 128;
            r.perBlock = [&] (AfterHitEngine& e, size_t pos) { auto s = e.getSettings(); s.hitPct = pos < ms (1000) ? 80.0f : 0.0f; e.setSettings (s); };
            const auto out = run (r, src);
            bool exact = true;
            for (size_t i = ms (1200); i < src.size(); ++i) if (out.l[i] != src.l[i] || out.r[i] != src.r[i]) { exact = false; break; }
            check (exact, "HIT automated 80% -> 0%: exact again once the 20 ms ramp ends");
        }
    }

    // 2 -------------------------------------------------------------------------
    section ("2. Post-hit timing");
    {
        Stereo in ((size_t) (1.5 * fs));
        const size_t hitAt = ms (200);
        addClap (in, fs, hitAt);
        const size_t firstIn = firstAbove (in, 0.0);

        Run r; r.s.hitPct = 0;     // dry untouched, so out - in is exactly the wet
        AfterHitEngine e;
        const auto out = run (r, in, &e);
        const auto w = wetOf (out, in);
        check (e.getOnsetCount() == 1, "one clap -> one onset", std::to_string (e.getOnsetCount()));

        const double protectPk = peakIn (w, hitAt, hitAt + ms (24));
        check (protectPk < 1.0e-6, "AFTER 25 ms: wet silent from the hit to +24 ms", fmt ("%.1f dBFS", db (protectPk)));
        const size_t wetStart = firstAbove (w, 1.0e-4);
        const double startMs = ((double) wetStart - (double) hitAt) * 1000.0 / fs;
        check (startMs >= 24.0 && startMs <= 32.0, "first audible wet (> -80 dBFS) arrives just after AFTER", fmt ("%.1f ms after the hit", startMs));
        const double bloom = rmsIn (w, hitAt + ms (35), hitAt + ms (200));
        check (bloom > 0.005, "wet clearly present after the gap", fmt ("RMS %.1f dBFS", db (bloom)));

        //  Dry onset untouched in position, with HIT on.
        Run h; h.s.hitPct = 100; h.s.spacePct = 0;
        const auto oh = run (h, in);
        check (firstAbove (oh, 0.0) == firstIn, "dry onset stays at the input sample (HIT 100, zero latency)",
               std::to_string (firstAbove (oh, 0.0)) + " vs " + std::to_string (firstIn));

        //  The reverb is excited by the attack itself (look-back): a 1-sample
        //  click still produces a tail.
        Stereo click ((size_t) (1.0 * fs));
        click.l[ms (100)] = click.r[ms (100)] = 0.8f;
        Run c; c.s.hitPct = 0; c.s.spacePct = 60;
        AfterHitEngine ec;
        const auto oc = run (c, click, &ec);
        const double clickTail = rmsIn (wetOf (oc, click), ms (130), ms (400));
        check (ec.getOnsetCount() == 1 && clickTail > 1.0e-4, "a single-sample click is detected and excites the reverb",
               fmt ("tail RMS %.1f dBFS", db (clickTail)));

        //  AFTER sweep.
        for (float after : { 0.0f, 10.0f, 60.0f, 100.0f })
        {
            Run a; a.s.hitPct = 0; a.s.afterMs = after; a.s.spacePct = 50;
            const auto oa = run (a, in);
            const auto wa = wetOf (oa, in);
            const double st = ((double) firstAbove (wa, 1.0e-4, hitAt) - (double) hitAt) * 1000.0 / fs;
            const double lo = std::max (0.0, (double) after - 1.0), hi = std::max ((double) after, (double) law::kLookbackMs) + 8.0;
            char label[96]; std::snprintf (label, sizeof label, "AFTER %.0f ms: wet starts in [%.0f, %.0f] ms", after, lo, hi);
            check (st >= lo && st <= hi, label, fmt ("%.1f ms", st));
        }
    }

    // 3 -------------------------------------------------------------------------
    section ("3. TAIL and GATE are independent; the close is smooth");
    {
        Stereo in ((size_t) (8.0 * fs));
        const size_t hitAt = ms (100);
        addClap (in, fs, hitAt);

        //  Decay is measured on a click (so the source's own body does not
        //  count as reverb) at TONE 100 (so the highs decay with the body).
        Stereo click ((size_t) (8.0 * fs));
        click.l[hitAt] = click.r[hitAt] = 0.8f;
        auto decayFor = [&] (float tail, float gate)
        {
            Run r; r.s.hitPct = 0; r.s.spacePct = 60; r.s.tailS = tail; r.s.gatePct = gate; r.s.sync = false; r.s.tonePct = 100;
            const auto w = wetOf (run (r, click), click);
            return decayTime (w, fs, hitAt, 30.0);
        };
        const double d05 = decayFor (0.5f, 0.0f), d20 = decayFor (2.0f, 0.0f), d015 = decayFor (0.15f, 0.0f);
        check (d20 / d05 > 2.8 && d20 / d05 < 5.5, "GATE off: TAIL 0.5 -> 2.0 s stretches the 30 dB decay ~4x",
               fmt2 ("%.3f -> %.3f s", d05, d20));
        check (d015 < 0.12, "TAIL 0.15 s really is short (30 dB decay)", fmt ("%.3f s", d015));

        double prev = 1e9; bool mono = true; std::string list;
        for (float gate : { 10.0f, 40.0f, 70.0f, 100.0f })
        {
            const double d = decayFor (2.0f, gate);
            list += fmt ("%.0f ", d * 1000.0);
            if (d >= prev) mono = false;
            prev = d;
        }
        check (mono, "TAIL 2 s fixed: GATE 10/40/70/100% shortens the audible window", list + "ms");
        const double dMax = decayFor (2.0f, 100.0f);
        check (dMax >= 0.06 && dMax <= 0.14, "GATE 100% (SYNC off): tight ~80 ms cut", fmt ("%.0f ms", dMax * 1000.0));

        //  Changing TAIL with GATE fixed barely moves the close (the gate rules).
        const double g1 = decayFor (1.0f, 70.0f), g4 = decayFor (4.0f, 70.0f);
        check (std::abs (g4 - g1) < 0.06, "GATE 70% fixed: TAIL 1 -> 4 s leaves the close where it was", fmt2 ("%.3f vs %.3f s", g1, g4));

        //  Smoothness: return-gain slope, sample by sample.
        Run r; r.s.hitPct = 0; r.s.spacePct = 60; r.s.gatePct = 100; r.s.sync = false; r.block = 1;
        double maxStep = 0.0; float last = -1.0f;
        r.perBlock = [&] (AfterHitEngine& e, size_t) {
            const float g = e.getReturnGain();
            if (last >= 0.0f) maxStep = std::max (maxStep, (double) std::abs (g - last));
            last = g; };
        run (r, in);
        check (maxStep < 0.03, "return gate never steps: max change per sample", fmt ("%.4f", maxStep));

        //  The close carries no click: wet HF energy around the close vs just before.
        Run hf; hf.s.hitPct = 0; hf.s.spacePct = 60; hf.s.tailS = 3.0f; hf.s.gatePct = 60; hf.s.sync = false;
        const auto w = wetOf (run (hf, in), in);
        auto hfEnergy = [&] (size_t a, size_t b) { double s = 0; for (size_t i = a + 1; i < b; ++i) { const double d = w.l[i] - w.l[i - 1]; s += d * d; } return s / (double) (b - a); };
        const double win = law::gateWindowMs (60.0f, false, 120.0);
        const size_t closeAt = hitAt + ms (25.0 + law::gateHoldMs ((float) win));
        const double before = hfEnergy (closeAt - ms (30), closeAt), during = hfEnergy (closeAt, closeAt + ms (30));
        check (during <= before * 1.05, "no click at the close: HF energy during close <= before", fmt2 ("%.1f vs %.1f dB", db (std::sqrt (during)), db (std::sqrt (before))));
    }

    // 4 -------------------------------------------------------------------------
    section ("4. Retrigger and rolls");
    {
        Stereo in ((size_t) (2.0 * fs));
        const size_t t1 = ms (100), t2 = ms (250);
        addClap (in, fs, t1, 0.5f, 5);
        addClap (in, fs, t2, 0.5f, 9);
        Run r; r.s.hitPct = 0; r.s.spacePct = 50; r.s.gatePct = 0; r.s.tailS = 1.5f;
        AfterHitEngine e;
        const auto w = wetOf (run (r, in, &e), in);
        check (e.getOnsetCount() == 2, "two claps 150 ms apart -> two onsets", std::to_string (e.getOnsetCount()));
        const double before = rmsIn (w, t2 - ms (20), t2);
        const double ducked = peakIn (w, t2 + ms (8), t2 + ms (24));
        check (db (ducked) < db (before) - 20.0, "old tail ducked > 20 dB under the second attack",
               fmt2 ("%.1f vs %.1f dBFS", db (ducked), db (before)));
        const double reopen = rmsIn (w, t2 + ms (40), t2 + ms (140));
        check (reopen > before * 0.7, "wet reopens after AFTER with the new tail", fmt2 ("%.1f vs %.1f dBFS", db (reopen), db (before)));

        //  Duck is smooth: no single-sample jump bigger than the signal's own.
        double maxJump = 0.0;
        for (size_t i = t2 - ms (2); i < t2 + ms (10); ++i) maxJump = std::max (maxJump, (double) std::abs (w.l[i] - w.l[i - 1]));
        double ownJump = 0.0;
        for (size_t i = t2 - ms (40); i < t2 - ms (5); ++i) ownJump = std::max (ownJump, (double) std::abs (w.l[i] - w.l[i - 1]));
        check (maxJump <= ownJump * 1.1, "duck adds no step larger than the tail's own sample-to-sample motion", fmt2 ("%.4f vs %.4f", maxJump, ownJump));

        //  Roll: 32 claps on 1/16 at 120 BPM, default settings.
        auto rollTest = [&] (float gate, float tail, double& ratioDb)
        {
            Stereo roll ((size_t) (5.5 * fs));
            for (int k = 0; k < 32; ++k) addClap (roll, fs, ms (100 + 125 * k), 0.5f, (juce::uint32) (20 + k));
            Stereo one ((size_t) (5.5 * fs));
            addClap (one, fs, ms (100), 0.5f, 20);
            Run rr; rr.s.hitPct = 0; rr.s.gatePct = gate; rr.s.tailS = tail;
            AfterHitEngine er;
            const auto wr = wetOf (run (rr, roll, &er), roll);
            const auto w1 = wetOf (run (rr, one), one);
            ratioDb = db (rmsIn (wr, ms (3100), ms (4100))) - db (rmsIn (w1, ms (100), ms (225)));
            return er.getOnsetCount();
        };
        double ratio = 0.0;
        const int n = rollTest (40.0f, 1.0f, ratio);
        check (n == 32, "roll: 32 hits -> 32 onsets", std::to_string (n));
        check (ratio < 3.0, "roll (defaults): steady wet within 3 dB of one hit's wet - no pile-up", fmt ("%+.1f dB", ratio));
        rollTest (40.0f, 6.0f, ratio);
        check (ratio < 3.0, "roll, TAIL 6 s + GATE 40%: choke keeps it bounded", fmt ("%+.1f dB", ratio));
        rollTest (0.0f, 6.0f, ratio);
        check (ratio < 14.0, "roll, TAIL 6 s + GATE OFF: natural build-up, bounded (physics, not runaway)", fmt ("%+.1f dB", ratio));
    }

    // 5 -------------------------------------------------------------------------
    section ("5. Detector");
    {
        auto onsets = [&] (const Stereo& in, float sens, double rate = 48000.0, int block = 256)
        {
            Run r; r.s.sensPct = sens; r.fs = rate; r.block = block;
            AfterHitEngine e; run (r, in, &e);
            return e.getOnsetCount();
        };
        Stereo silence ((size_t) (5.0 * fs));
        check (onsets (silence, 100.0f) == 0, "silence -> no onsets (sensitivity 100%)");

        Stereo noise ((size_t) (10.0 * fs)); Rng rng { 99 };
        for (size_t i = 0; i < noise.size(); ++i) { noise.l[i] = 0.1f * rng.next(); noise.r[i] = 0.1f * rng.next(); }
        const int nDef = onsets (noise, 50.0f), nHigh = onsets (noise, 100.0f);
        check (nDef <= 1, "steady noise -20 dBFS, 10 s, default: at most the start", std::to_string (nDef));
        check (nHigh <= 1, "steady noise -20 dBFS, 10 s, sensitivity 100%", std::to_string (nHigh));

        Stereo tone ((size_t) (10.0 * fs));
        for (size_t i = 0; i < tone.size(); ++i) tone.l[i] = tone.r[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 200.0 * (double) i / fs);
        const int nTone = onsets (tone, 100.0f);
        check (nTone <= 1, "steady 200 Hz tone, 10 s, sensitivity 100%", std::to_string (nTone));

        Stereo soft ((size_t) (2.0 * fs));
        addClap (soft, fs, ms (300), 0.006f);   // about -45 dBFS
        const int s0 = onsets (soft, 0.0f), s100 = onsets (soft, 100.0f);
        check (s0 == 0 && s100 == 1, "soft hit (-45 dBFS): ignored at 0%, caught at 100%", std::to_string (s0) + " / " + std::to_string (s100));

        Stereo asym ((size_t) (1.0 * fs));
        addClap (asym, fs, ms (200), 0.5f, 1, true, false);
        addClap (asym, fs, ms (203), 0.5f, 2, false, true);
        check (onsets (asym, 50.0f) == 1, "left hit + right hit 3 ms later -> ONE linked event");

        Stereo kicks ((size_t) (4.0 * fs));
        for (int k = 0; k < 6; ++k) addKick (kicks, fs, ms (100 + 600 * k));
        check (onsets (kicks, 50.0f) == 6, "6 kicks with long bodies -> 6 onsets", std::to_string (onsets (kicks, 50.0f)));

        Stereo fast ((size_t) (2.0 * fs));
        for (int k = 0; k < 16; ++k) addClap (fast, fs, ms (100 + 54 * k), 0.4f, (juce::uint32) (40 + k));
        check (onsets (fast, 50.0f) == 16, "1/32 roll at 140 BPM (54 ms) -> 16 distinct hits", std::to_string (onsets (fast, 50.0f)));

        //  Sample-rate and block-size independence.
        std::string counts; bool same = true; int ref = -1;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto loop = drumLoop (rate, 6.0);
            const int n = onsets (loop, 50.0f, rate);
            counts += std::to_string (n) + " ";
            if (ref < 0) ref = n; else if (n != ref) same = false;
        }
        check (same, "drum loop: same onset count at 44.1 / 48 / 96 kHz", counts);

        const auto loop = drumLoop (fs, 4.0);
        Run a; a.block = 1;   Run b; b.block = 13;   Run c; c.block = 4096;
        const auto oa = run (a, loop), ob = run (b, loop), oc = run (c, loop);
        check (identical (oa, ob) && identical (ob, oc), "output bit-identical for block sizes 1 / 13 / 4096 (defaults)");
    }

    // 6 -------------------------------------------------------------------------
    section ("6. Host edges: automation, tempo, rates, tiny blocks");
    {
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto loop = drumLoop (rate, 5.0);
            Rng rng { (juce::uint32) rate };
            Run r; r.fs = rate; r.block = 64;
            r.perBlock = [&] (AfterHitEngine& e, size_t)
            {
                auto s = e.getSettings();
                auto u = [&] { return 0.5f + 0.5f * rng.next(); };
                if (u() < 0.3f) s.hitPct = 100 * u();
                if (u() < 0.3f) s.spacePct = 100 * u();
                if (u() < 0.3f) s.tailS = law::kTailMin + (law::kTailMax - law::kTailMin) * u();
                if (u() < 0.3f) s.gatePct = 100 * u();
                if (u() < 0.3f) s.afterMs = 100 * u();
                if (u() < 0.3f) s.sensPct = 100 * u();
                if (u() < 0.3f) s.tonePct = 100 * u();
                if (u() < 0.3f) s.widthPct = 150 * u();
                if (u() < 0.1f) s.sync = ! s.sync;
                if (u() < 0.3f) s.outDb = -12 + 18 * u();
                if (u() < 0.05f) s.bypass = ! s.bypass;
                if (u() < 0.2f) s.bpm = 60 + 140 * u();
                e.setSettings (s);
            };
            const auto out = run (r, loop);
            const double pk = peakIn (out, 0, out.size());
            char label[96]; std::snprintf (label, sizeof label, "%.1f kHz: every control automated per block -> finite, bounded", rate / 1000.0);
            check (finite (out) && pk < 6.0, label, fmt ("peak %.1f dBFS", db (pk)));
        }

        //  Block size 1 with everything moving, mono.
        {
            const auto loop = drumLoop (fs, 2.0);
            Run r; r.block = 1; r.mono = true; r.s.spacePct = 100; r.s.hitPct = 100; r.s.widthPct = 150;
            Stereo src = loop; src.r = src.l;
            const auto out = run (r, src);
            check (finite (out) && peakIn (out, 0, out.size()) < 6.0, "mono, block size 1, HIT/SPACE 100%: finite");
        }

        //  Silence in -> exact silence out, eventually, even with the longest tail.
        {
            Stereo in ((size_t) (40.0 * fs));
            addClap (in, fs, ms (100), 0.9f);
            Run r; r.s.tailS = 6.0f; r.s.gatePct = 0; r.s.spacePct = 100; r.block = 512;
            AfterHitEngine e;
            const auto out = run (r, in, &e);
            size_t lastNonZero = 0;
            for (size_t i = 0; i < out.size(); ++i) if (out.l[i] != 0.0f || out.r[i] != 0.0f) lastNonZero = i;
            check (lastNonZero < out.size() - (size_t) fs, "TAIL 6 s: output reaches exact 0.0 after silence", fmt ("last non-zero at %.1f s", (double) lastNonZero / fs));
            check (e.isReverbAsleep(), "both tanks asleep afterwards (no CPU spent on silence)");
        }

        //  Tempo: the synced window follows the host, the division does not change.
        {
            AfterHitEngine e; auto s = AfterHitEngine::Settings(); s.bpm = 120.0; e.setSettings (s); e.prepare (fs, 256, 2);
            Stereo quiet ((size_t) fs);
            float* ch[2] = { quiet.l.data(), quiet.r.data() };
            e.process (ch, 2, 4800);
            const float w120 = e.getWindowMs();
            s.bpm = 140.0; e.setSettings (s);
            e.process (ch, 2, 48000 - 4800);
            const float w140 = e.getWindowMs();
            check (std::abs (w120 - 250.0f) < 0.5f && std::abs (w140 - 214.29f) < 0.5f, "SYNC 1/8: 250 ms at 120 BPM, glides to 214 ms at 140",
                   fmt2 ("%.1f / %.1f ms", w120, w140));
        }
    }

    // 7 -------------------------------------------------------------------------
    section ("7. Level, HIT, SPACE, TONE, WIDTH");
    {
        Stereo in ((size_t) (2.0 * fs));
        addKick (in, fs, ms (100), 0.99f);
        addClap (in, fs, ms (900), 0.99f);
        Run r; r.s.hitPct = 100; r.s.spacePct = 100;
        const auto out = run (r, in);
        std::printf ("  loud kick + clap at -0.1 dBFS, HIT 100 SPACE 100: output peak %.1f dBFS (no limiter; honest)\n", db (peakIn (out, 0, out.size())));
        check (finite (out), "loud input at full HIT/SPACE: finite");

        //  HIT: up to +6 dB on the attack, ~0 dB on the body.
        Stereo clap ((size_t) (1.0 * fs));
        addClap (clap, fs, ms (100));
        Run h; h.s.hitPct = 100; h.s.spacePct = 0; h.block = 1;
        double maxDb = -100.0, bodyDb = -100.0;
        h.perBlock = [&] (AfterHitEngine& e, size_t pos)
        {
            const double g = e.getHitGainDb();
            maxDb = std::max (maxDb, g);
            if (pos > ms (180) && pos < ms (220)) bodyDb = std::max (bodyDb, g);
        };
        run (h, clap);
        check (maxDb > 5.0 && maxDb <= 6.0001, "HIT 100%: attack emphasis reaches ~+6 dB, never more", fmt ("%.2f dB", maxDb));
        check (bodyDb < 0.5, "HIT 100%: the body 80-120 ms after the hit is not lifted", fmt ("%.2f dB", bodyDb));

        //  SPACE taper.
        auto wetRms = [&] (float space)
        {
            Run s; s.s.hitPct = 0; s.s.spacePct = space;
            const auto w = wetOf (run (s, clap), clap);
            return rmsIn (w, ms (125), ms (400));
        };
        const double w10 = wetRms (10.0f), w28 = wetRms (28.0f), w100 = wetRms (100.0f);
        check (db (w100) - db (w10) > 30.0 && db (w100) - db (w10) < 50.0, "SPACE 10% is ~40 dB under 100% (no jump at the start)", fmt ("%.1f dB", db (w100) - db (w10)));
        check (w10 < w28 && w28 < w100, "SPACE is monotonic");

        //  TONE: wet brightness.
        auto brightness = [&] (float tone)
        {
            Run s; s.s.hitPct = 0; s.s.spacePct = 60; s.s.tonePct = tone;
            const auto w = wetOf (run (s, clap), clap);
            double diff = 0, all = 0;
            for (size_t i = ms (130); i < ms (500); ++i) { const double d = w.l[i] - w.l[i - 1]; diff += d * d; all += (double) w.l[i] * w.l[i]; }
            return diff / all;
        };
        const double dark = brightness (0.0f), bright = brightness (100.0f);
        check (bright > dark * 2.0, "TONE 0 -> 100%: wet HF/total ratio rises", fmt2 ("%.4f -> %.4f", dark, bright));

        //  WIDTH: wet side/mid.
        auto sideRatio = [&] (float width)
        {
            Run s; s.s.hitPct = 0; s.s.spacePct = 60; s.s.widthPct = width;
            const auto w = wetOf (run (s, clap), clap);
            double m = 0, sd = 0;
            for (size_t i = ms (130); i < ms (600); ++i) { const double a = w.l[i] + w.r[i], b = w.l[i] - w.r[i]; m += a * a; sd += b * b; }
            return std::sqrt (sd / m);
        };
        const double s0 = sideRatio (0.0f), s100 = sideRatio (100.0f), s150 = sideRatio (150.0f);
        check (s0 < 1.0e-3 && s100 > 0.3 && s150 > s100 * 1.3, "WIDTH 0 / 100 / 150%: mono / wide / wider wet", fmt2 ("%.3f / %.3f", s100, s150));
    }

    // 8 -------------------------------------------------------------------------
    section ("8. GATE law");
    {
        bool monotonic = true;
        double prevRef = 1e9, prevBeats = 1e9;
        for (int k = 1; k <= 1000; ++k)
        {
            const float g = (float) k * 0.1f;
            const double ref = law::gateReferenceMs (g);
            const double beats = law::kDivisions[law::gateDivisionIndex (g)].beats;
            if (ref > prevRef + 1e-6 || beats > prevBeats + 1e-9) monotonic = false;
            prevRef = ref; prevBeats = beats;
        }
        check (monotonic, "GATE 0.1..100%: window never lengthens as the knob turns up (both modes)");
        check (law::gateIsOff (0.0f) && ! law::gateIsOff (1.0f), "GATE 0% = OFF");
        check (std::string (law::kDivisions[law::gateDivisionIndex (law::kGateDef)].name) == "1/8", "default 40% = 1/8 (250 ms at 120 BPM)");
        check (std::string (law::kDivisions[law::gateDivisionIndex (100.0f)].name) == "1/32"
               && std::string (law::kDivisions[law::gateDivisionIndex (1.0f)].name) == "1 bar", "range: 1 bar .. 1/32");
        check (std::abs (law::gateReferenceMs (100.0f) - 80.0f) < 0.01f, "SYNC off at 100%: 80 ms");
        bool inv = true;
        for (float g : { 5.0f, 25.0f, 40.0f, 63.0f, 99.0f })
            if (std::abs (law::gatePctForReferenceMs (law::gateReferenceMs (g)) - g) > 0.01f) inv = false;
        check (inv, "ms -> GATE% inverse round-trips");
    }

    // 9 -------------------------------------------------------------------------
    section ("9. CPU (one stereo instance, 48 kHz, 512-sample blocks, defaults)");
    {
        const auto loop = drumLoop (fs, 30.0);
        Run r; r.block = 512;
        AfterHitEngine e;
        e.setSettings (r.s); e.prepare (fs, 512, 2);
        Stereo buf = loop;
        const auto t0 = std::chrono::steady_clock::now();
        process (e, r, buf);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("  30 s of audio in %.3f s: %.2f %% of one core (%.0fx real time)\n", secs, 100.0 * secs / 30.0, 30.0 / secs);
        check (secs < 30.0, "faster than real time");
    }

    std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
