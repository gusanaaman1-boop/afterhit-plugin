// AFTERHIT host-contract suite.
//
//     build/AfterHitHostTests_artefacts/<config>/AfterHitHostTests
//
// AfterHitTests measures the DSP. This drives the whole AudioProcessor the way
// a host does - parameters, prepare/process lifecycle, bus layouts, state,
// programs, bypass, tempo, the editor - and asserts it directly. Exit 0 = pass.

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include <AfterHitVersion.h>

#include "../Core/ParameterIds.h"
#include "../Core/Presets.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    //  Thread-local: the claim is "the audio thread does not allocate", and
    //  this test's calling thread stands in for it. JUCE's own background
    //  threads may allocate whenever they like.
    thread_local int gAllocations = 0;
    thread_local bool gCountAllocations = false;
}

void* operator new (std::size_t n)
{
    if (gCountAllocations)
        ++gAllocations;
    if (auto* p = std::malloc (n == 0 ? 1 : n))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    int gChecks = 0, gFailures = 0;

    void check (bool ok, const char* label, const juce::String& measured = {})
    {
        ++gChecks;
        if (! ok) ++gFailures;
        std::printf ("  [%s] %-64s %s\n", ok ? "PASS" : "FAIL", label, measured.toRawUTF8());
    }
    void section (const char* name) { std::printf ("\n== %s ==\n", name); }

    const char* const kAllIds[] = {
        ah::id::hit, ah::id::space, ah::id::tail, ah::id::gate,
        ah::id::after, ah::id::sensitivity, ah::id::tone, ah::id::width, ah::id::sync, ah::id::output,
        ah::id::bypass };
    constexpr int kNumIds = (int) (sizeof (kAllIds) / sizeof (kAllIds[0]));

    void setParam (AfterHitAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
    float getParam (AfterHitAudioProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f;
    }

    void applyBusy (AfterHitAudioProcessor& p)
    {
        setParam (p, ah::id::hit, 63.0f);   setParam (p, ah::id::space, 71.0f);
        setParam (p, ah::id::tail, 2.7f);   setParam (p, ah::id::gate, 77.0f);
        setParam (p, ah::id::after, 41.0f); setParam (p, ah::id::sensitivity, 12.0f);
        setParam (p, ah::id::tone, 88.0f);  setParam (p, ah::id::width, 131.0f);
        setParam (p, ah::id::sync, 0.0f);   setParam (p, ah::id::output, -4.5f);
    }

    void fillDrums (juce::AudioBuffer<float>& b, int startSample, double fs)
    {
        //  A clap every 250 ms, deterministic.
        juce::uint32 seed = 5;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const int t = startSample + i;
            const int inBeat = t % (int) (0.25 * fs);
            const float env = std::exp (-(float) inBeat / (float) (0.01 * fs));
            seed = seed * 1664525u + 1013904223u;
            const float v = 0.5f * env * ((float) ((double) (seed >> 8) / 8388608.0 - 1.0));
            for (int c = 0; c < b.getNumChannels(); ++c)
                b.setSample (c, i, v * (c == 0 ? 1.0f : 0.8f));
        }
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i))) return false;
        return true;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        juce::Optional<double> bpm;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i;
            if (bpm) i.setBpm (*bpm);
            return i;
        }
    };

    void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    std::printf ("AFTERHIT %s (%s, %s) - host-contract suite\n", ah::kVersion, ah::kGitDescribe, ah::kBuildDate);

    // 1 -------------------------------------------------------------------------
    section ("1. Parameter contract");
    {
        AfterHitAudioProcessor p;
        check (p.getParameters().size() == kNumIds, "parameter count", juce::String (p.getParameters().size()));
        bool allThere = true;
        for (auto* id : kAllIds) if (p.getState().getParameter (id) == nullptr) { allThere = false; std::printf ("      missing %s\n", id); }
        check (allThere, "every frozen ID resolves");

        bool named = true, automatable = true;
        for (auto* raw : p.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr || rp->getName (64).isEmpty()) named = false;
            if (! raw->isAutomatable()) automatable = false;
        }
        check (named && automatable, "every parameter named and automatable");

        struct Spec { const char* id; float lo, hi, def; };
        const Spec specs[] = {
            { ah::id::hit, 0, 100, 20 }, { ah::id::space, 0, 100, 28 }, { ah::id::tail, 0.15f, 6.0f, 1.0f },
            { ah::id::gate, 0, 100, 40 }, { ah::id::after, 0, 100, 25 }, { ah::id::sensitivity, 0, 100, 50 },
            { ah::id::tone, 0, 100, 45 }, { ah::id::width, 0, 150, 100 }, { ah::id::output, -12, 6, 0 } };
        bool rangesOk = true;
        for (const auto& s : specs)
        {
            auto* rp = p.getState().getParameter (s.id);
            const float lo = rp->convertFrom0to1 (0.0f), hi = rp->convertFrom0to1 (1.0f), def = rp->convertFrom0to1 (rp->getDefaultValue());
            if (std::abs (lo - s.lo) > 1e-4f || std::abs (hi - s.hi) > 1e-4f || std::abs (def - s.def) > 1e-3f)
            {
                rangesOk = false;
                std::printf ("      %s: %g..%g def %g\n", s.id, lo, hi, def);
            }
        }
        check (rangesOk, "ranges and defaults match the spec (section 4)");
        check (getParam (p, ah::id::sync) > 0.5f && getParam (p, ah::id::bypass) < 0.5f, "SYNC defaults On, bypass Off");

        auto text = [&] (const char* id, float v) { auto* rp = p.getState().getParameter (id); return rp->getText (rp->convertTo0to1 (v), 0); };
        check (text (ah::id::gate, 40.0f) == "1/8 (250 ms)", "GATE host text at default", text (ah::id::gate, 40.0f));
        check (text (ah::id::gate, 0.0f) == "OFF", "GATE 0 reads OFF");
        check (text (ah::id::tail, 1.0f) == "1.0 s" && text (ah::id::hit, 20.0f) == "20%" && text (ah::id::after, 25.0f) == "25 ms"
               && text (ah::id::output, -3.0f) == "-3.0 dB", "readable host values", text (ah::id::tail, 1.0f) + ", " + text (ah::id::output, -3.0f));

        struct RT { const char* id; float v; float tol; };
        const RT rts[] = { { ah::id::hit, 37.0f, 0.6f }, { ah::id::tail, 0.45f, 0.006f }, { ah::id::tail, 3.2f, 0.06f },
                           { ah::id::after, 12.0f, 0.6f }, { ah::id::output, -6.5f, 0.06f }, { ah::id::width, 140.0f, 0.6f },
                           { ah::id::gate, 40.0f, 0.3f }, { ah::id::gate, 100.0f, 0.3f } };
        for (const auto& rt : rts)
        {
            auto* rp = p.getState().getParameter (rt.id);
            const auto t = rp->getText (rp->convertTo0to1 (rt.v), 0);
            const float back = rp->convertFrom0to1 (rp->getValueForText (t));
            //  GATE's text names a division + ms, which parses to the middle of
            //  that division's band: compare the division instead.
            const bool ok = juce::String (rt.id) == ah::id::gate
                ? rp->getText (rp->convertTo0to1 (back), 0).upToFirstOccurrenceOf ("(", false, false)
                    == t.upToFirstOccurrenceOf ("(", false, false)
                : std::abs (back - rt.v) <= rt.tol;
            char label[96]; std::snprintf (label, sizeof label, "text round trip %s: %s", rt.id, t.toRawUTF8());
            check (ok, label, "-> " + juce::String (back, 3));
        }
        auto* gp = p.getState().getParameter (ah::id::gate);
        check (ah::law::kDivisions[ah::law::gateDivisionIndex (gp->convertFrom0to1 (gp->getValueForText ("1/16")))].beats == 0.25f,
               "typing '1/16' into GATE selects 1/16");
        check (std::abs (ah::law::gateReferenceMs (gp->convertFrom0to1 (gp->getValueForText ("120 ms"))) - 120.0f) < 1.0f,
               "typing '120 ms' into GATE = 120 ms window");
    }

    // 2 -------------------------------------------------------------------------
    section ("2. Buses, latency, tail");
    {
        AfterHitAudioProcessor p;
        using CS = juce::AudioChannelSet;
        auto layout = [] (CS in, CS out) { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l; };
        check (p.checkBusesLayoutSupported (layout (CS::mono(), CS::mono())), "mono -> mono supported");
        check (p.checkBusesLayoutSupported (layout (CS::stereo(), CS::stereo())), "stereo -> stereo supported");
        check (! p.checkBusesLayoutSupported (layout (CS::mono(), CS::stereo())), "mono -> stereo rejected (in = out)");
        check (! p.checkBusesLayoutSupported (layout (CS::create5point1(), CS::create5point1())), "5.1 rejected");
        p.prepareToPlay (48000.0, 512);
        check (p.getLatencySamples() == 0, "reported latency 0 samples", juce::String (p.getLatencySamples()));
        setParam (p, ah::id::tail, 6.0f);
        check (p.getTailLengthSeconds() >= 6.0, "tail length covers TAIL (for offline bounce)", juce::String (p.getTailLengthSeconds(), 2) + " s");
        check (p.getBypassParameter() == p.getState().getParameter (ah::id::bypass), "host bypass is the plug-in's bypass parameter");
    }

    // 3 -------------------------------------------------------------------------
    section ("3. Neutral and bypass through the processor");
    {
        for (int ch : { 1, 2 })
            for (double rate : { 44100.0, 96000.0 })
            {
                AfterHitAudioProcessor p;
                p.applyPreset (ah::presets::neutralIndex());
                p.setPlayConfigDetails (ch, ch, rate, 512);
                p.prepareToPlay (rate, 512);
                juce::MidiBuffer midi;
                bool exact = true; int pos = 0;
                for (int blk = 0; blk < 200; ++blk)
                {
                    const int n = 1 + (blk * 37) % 512;
                    juce::AudioBuffer<float> in (ch, n), out (ch, n);
                    fillDrums (in, pos, rate); pos += n;
                    out.makeCopyOf (in);
                    p.processBlock (out, midi);
                    for (int c = 0; c < ch; ++c)
                        for (int i = 0; i < n; ++i)
                            if (out.getSample (c, i) != in.getSample (c, i)) exact = false;
                }
                char label[96]; std::snprintf (label, sizeof label, "Neutral preset, %s, %.1f kHz, varying blocks: exact", ch == 1 ? "mono" : "stereo", rate / 1000.0);
                check (exact, label);
            }

        AfterHitAudioProcessor p;
        applyBusy (p);
        p.prepareToPlay (48000.0, 256);
        juce::MidiBuffer midi;
        int pos = 0;
        for (int blk = 0; blk < 50; ++blk) { juce::AudioBuffer<float> b (2, 256); fillDrums (b, pos, 48000.0); pos += 256; p.processBlock (b, midi); }
        setParam (p, ah::id::bypass, 1.0f);
        bool exact = true, finite = true;
        for (int blk = 0; blk < 100; ++blk)
        {
            juce::AudioBuffer<float> in (2, 256), out (2, 256);
            fillDrums (in, pos, 48000.0); pos += 256;
            out.makeCopyOf (in);
            p.processBlock (out, midi);
            finite = finite && allFinite (out);
            if (blk > 4)
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) if (out.getSample (c, i) != in.getSample (c, i)) exact = false;
        }
        check (finite && exact, "bypass on: output == input once the 10 ms crossfade ends");
    }

    // 4 -------------------------------------------------------------------------
    section ("4. State and programs");
    {
        AfterHitAudioProcessor a;
        applyBusy (a);
        a.advancedOpen = true;
        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        AfterHitAudioProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());
        bool same = true;
        for (auto* id : kAllIds)
            if (std::abs (getParam (a, id) - getParam (b, id)) > 1e-4f) { same = false; std::printf ("      %s differs\n", id); }
        check (same, "every parameter survives save -> load");
        check (b.advancedOpen.load(), "ADVANCED open/closed survives save -> load");

        //  Restored host state beats the factory preset: a host re-sending the
        //  saved program number must not re-apply the preset.
        b.setCurrentProgram (b.getCurrentProgram());
        check (std::abs (getParam (b, ah::id::space) - 71.0f) < 0.01f, "re-sent program number after load leaves restored values alone");

        check (a.getNumPrograms() == 7, "7 factory programs", juce::String (a.getNumPrograms()));
        juce::StringArray names;
        for (int i = 0; i < a.getNumPrograms(); ++i) names.add (a.getProgramName (i));
        check (names.joinIntoString (",") == "Wide Clap,Tight Kick,Short Snare,Perc Room,Dub Hit,Big Fill,Neutral", "program names", names.joinIntoString (", "));

        AfterHitAudioProcessor fresh;
        bool defaultIsWideClap = true;
        AfterHitAudioProcessor wc; wc.applyPreset (1); wc.applyPreset (0);
        for (auto* id : kAllIds) if (std::abs (getParam (fresh, id) - getParam (wc, id)) > 1e-3f) defaultIsWideClap = false;
        check (defaultIsWideClap && fresh.getCurrentProgram() == 0, "a fresh instance IS 'Wide Clap' (program 0 = the defaults)");

        a.setCurrentProgram (1);
        check (std::abs (getParam (a, ah::id::tail) - 0.45f) < 0.005f && std::abs (getParam (a, ah::id::output) + 4.5f) < 0.01f,
               "host program change applies Tight Kick, keeps OUTPUT");
        a.setCurrentProgram (ah::presets::neutralIndex());
        check (getParam (a, ah::id::hit) == 0.0f && getParam (a, ah::id::space) == 0.0f && std::abs (getParam (a, ah::id::output)) < 1e-4f, "Neutral zeroes HIT, SPACE, OUTPUT");

        //  Garbage state is ignored, not trusted.
        AfterHitAudioProcessor g;
        const char junk[] = "not a state at all";
        g.setStateInformation (junk, (int) sizeof junk);
        check (std::abs (getParam (g, ah::id::space) - 28.0f) < 0.01f, "garbage state blob leaves defaults intact");
    }

    // 5 -------------------------------------------------------------------------
    section ("5. Audio thread: no allocation, tempo, telemetry");
    {
        AfterHitAudioProcessor p;
        FakePlayHead head; head.bpm = 140.0;
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 1024);
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (p.createEditor());
        juce::MidiBuffer midi;
        std::vector<juce::AudioBuffer<float>> bufs;
        for (int n : { 1, 7, 64, 256, 1024 }) bufs.emplace_back (2, n);
        int pos = 0; int allocs = 0;
        const auto id0 = p.getTelemetry().eventId.load();
        for (int round = 0; round < 60; ++round)
            for (auto& b : bufs)
            {
                fillDrums (b, pos, 48000.0); pos += b.getNumSamples();
                if (round % 7 == 3) applyBusy (p);      // parameter changes mid-stream
                if (round % 7 == 5) p.applyPreset (round % 6);
                gAllocations = 0; gCountAllocations = true;
                p.processBlock (b, midi);
                gCountAllocations = false;
                allocs += gAllocations;
            }
        check (allocs == 0, "processBlock allocates nothing (5 block sizes, editor open, automation)", juce::String (allocs));
        auto& t = p.getTelemetry();
        check (std::abs (t.bpm.load() - 140.0) < 1e-6 && t.bpmFromHost.load(), "host tempo 140 BPM reaches the engine");
        check (t.eventId.load() != id0 && t.filled.load() > ah::timeAxis::kPreBins, "telemetry: hits captured for the strip",
               juce::String ((int) (t.eventId.load() - id0)) + " events");

        head.bpm.reset();
        juce::AudioBuffer<float> b (2, 256); fillDrums (b, 0, 48000.0);
        p.processBlock (b, midi);
        check (std::abs (t.bpm.load() - 120.0) < 1e-6 && ! t.bpmFromHost.load(), "no host tempo: stable 120 BPM fallback, flagged");
        p.setPlayHead (nullptr);
    }

    // 6 -------------------------------------------------------------------------
    section ("6. Editor");
    {
        AfterHitAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<AfterHitAudioProcessorEditor*> (base.get());
        check (ed != nullptr, "editor created");
        check (ed->getWidth() == 640 && ed->getHeight() == 226, "closed size 640 x 226", juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()));

        juce::Rectangle<int> before[4];
        for (int i = 0; i < 4; ++i) before[i] = ed->mainDialBounds (i);
        bool sizeOk = true;
        for (int i = 0; i < 4; ++i) sizeOk = sizeOk && before[i].getWidth() >= 58 && before[i].getWidth() <= 64;
        check (sizeOk, "four main dials are 58-64 px", juce::String (before[0].getWidth()));

        ed->setAdvancedOpen (true);
        check (ed->getHeight() >= 285 && ed->getHeight() <= 310, "ADVANCED open: 285-310 px tall", juce::String (ed->getHeight()));
        bool unmoved = true;
        for (int i = 0; i < 4; ++i) unmoved = unmoved && ed->mainDialBounds (i) == before[i];
        check (unmoved, "opening ADVANCED does not move or shrink the four dials");
        check (p.advancedOpen.load(), "drawer state stored in the processor");
        ed->setAdvancedOpen (false);
        check (ed->getHeight() == 226, "closing restores the compact size");

        ed->pollNow();
        check (ed->valueText (0) == "20%" && ed->valueText (1) == "28%" && ed->valueText (2) == "1.0 s" && ed->valueText (3) == "1/8",
               "default read-outs: 20% / 28% / 1.0 s / 1/8",
               ed->valueText (0) + " / " + ed->valueText (1) + " / " + ed->valueText (2) + " / " + ed->valueText (3));
        check (ed->presetBox().getText() == "WIDE CLAP", "preset selector shows WIDE CLAP", ed->presetBox().getText());

        setParam (p, ah::id::sync, 0.0f);
        ed->pollNow();
        check (ed->presetBox().getText() == "WIDE CLAP *", "an edited preset is marked with *", ed->presetBox().getText());
        check (ed->valueText (3) == "250 ms", "SYNC off: GATE reads 250 ms", ed->valueText (3));
        setParam (p, ah::id::gate, 0.0f);
        ed->pollNow();
        check (ed->valueText (3) == "OFF", "GATE 0 reads OFF");

        //  A host automation change reaches the dial.
        setParam (p, ah::id::space, 64.0f);
        pump (100);
        ed->pollNow();
        check (ed->valueText (1) == "64%", "host automation shows on the dial", ed->valueText (1));

        //  Re-open with the drawer open (state).
        base.reset();
        p.advancedOpen = true;
        std::unique_ptr<juce::AudioProcessorEditor> again (p.createEditor());
        check (again->getHeight() > 226, "a new editor opens with ADVANCED as it was left");
        again.reset();

        //  Open / close editors repeatedly while processing.
        juce::MidiBuffer midi; bool ok = true; int pos = 0;
        for (int i = 0; i < 20; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
            juce::AudioBuffer<float> b (2, 512); fillDrums (b, pos, 48000.0); pos += 512;
            p.processBlock (b, midi);
            ok = ok && allFinite (b);
            pump (5);
        }
        check (ok, "20 editor open/close cycles while processing");
    }

    std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
