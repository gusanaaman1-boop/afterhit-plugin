// Deterministic UI renderer.
//
//     build/AfterHitShot_artefacts/<config>/AfterHitShot ui-shots        editor PNGs
//     build/AfterHitShot_artefacts/<config>/AfterHitShot --icon out.png  1024 px icon
//     build/AfterHitShot_artefacts/<config>/AfterHitShot --params out.md parameter table
//
// Builds the real editor over the real processor, feeds real audio through
// processBlock so the meters and the timing strip show measured data, and
// renders the component to PNG (1x and 2x). No screen capture, no timing luck.

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../Core/Presets.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    struct Shot
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
        bool audio = true;
        bool advanced = false;
        int preset = -1;
    };

    void setParam (AfterHitAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    //  One clap-like hit, then silence, so the strip holds a single real
    //  capture at the moment of the shot.
    void feedHit (AfterHitAudioProcessor& p, double seconds)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        juce::Random rng (7);
        const int total = (int) (seconds * 48000.0);
        const int hitAt = 2400;
        for (int pos = 0; pos < total; pos += 256)
        {
            for (int i = 0; i < 256; ++i)
            {
                const int t = pos + i - hitAt;
                float v = 0.0f;
                if (t >= 0)
                {
                    const float ts = (float) t / 48000.0f;
                    float env = 0.0f;
                    for (int k = 0; k < 3; ++k) { const float tk = ts - 0.008f * (float) k; if (tk >= 0.0f) env = juce::jmax (env, std::exp (-tk / 0.0018f)); }
                    if (ts >= 0.016f) env = juce::jmax (env, 0.55f * std::exp (-(ts - 0.016f) / 0.025f));
                    v = 0.5f * env * (rng.nextFloat() * 2.0f - 1.0f);
                }
                buf.setSample (0, i, v);
                buf.setSample (1, i, v * 0.9f);
            }
            p.processBlock (buf, midi);
        }
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    int render (const juce::File& outputDir)
    {
        const std::vector<Shot> shots =
        {
            { "default-model",   {}, false },
            { "default-hit",     {} },
            { "advanced-open",   {}, true, true },
            { "tight-kick",      {}, true, false, 1 },
            { "dub-hit-gate-off",{}, true, false, 4 },
            { "after-80-gate-max", { { ah::id::after, 80.0f }, { ah::id::gate, 100.0f }, { ah::id::sync, 0.0f } } },
            { "bypassed",        { { ah::id::bypass, 1.0f } } },
        };

        outputDir.createDirectory();

        for (const auto& shot : shots)
        {
            AfterHitAudioProcessor processor;
            processor.prepareToPlay (48000.0, 256);
            if (shot.preset >= 0)
                processor.applyPreset (shot.preset);
            for (const auto& v : shot.values)
                setParam (processor, v.first, v.second);
            processor.advancedOpen = shot.advanced;

            std::unique_ptr<juce::AudioProcessorEditor> base (processor.createEditor());
            auto* editor = dynamic_cast<AfterHitAudioProcessorEditor*> (base.get());
            if (editor == nullptr) { std::fprintf (stderr, "no editor\n"); return 1; }

            if (shot.audio)
            {
                feedHit (processor, 0.25);
                editor->pollNow();
                feedHit (processor, 0.0);
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            editor->pollNow();

            for (int scale : { 1, 2 })
            {
                juce::Image image (juce::Image::ARGB, editor->getWidth() * scale, editor->getHeight() * scale, true);
                {
                    juce::Graphics g (image);
                    g.addTransform (juce::AffineTransform::scale ((float) scale));
                    editor->paintEntireComponent (g, true);
                }
                const auto file = outputDir.getChildFile (juce::String (shot.name) + (scale == 2 ? "@2x" : "") + ".png");
                if (! writePng (image, file))
                {
                    std::fprintf (stderr, "could not write %s\n", file.getFullPathName().toRawUTF8());
                    return 1;
                }
                std::printf ("wrote %s  (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
            }
        }
        return 0;
    }

    int writeParameterTable (const juce::File& out)
    {
        AfterHitAudioProcessor processor;
        juce::StringArray rows;
        rows.add ("| Control | ID | Range | Default |");
        rows.add ("|---|---|---|---|");
        for (auto* raw : processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;
            const auto low = rp->getText (0.0f, 0), high = rp->getText (1.0f, 0), def = rp->getText (rp->getDefaultValue(), 0);
            rows.add ("| " + rp->getName (128) + " | `" + rp->paramID + "` | " + low + " … " + high + " | " + def + " |");
        }
        rows.add ("");
        rows.add ("| # | Preset | HIT | SPACE | TAIL | GATE | AFTER | SENS | TONE | WIDTH | SYNC |");
        rows.add ("|---|---|---|---|---|---|---|---|---|---|---|");
        for (int i = 0; i < ah::presets::count(); ++i)
        {
            const auto& p = ah::presets::kPresets[i];
            rows.add ("| " + juce::String (i) + " | " + p.name + " | " + juce::String (p.hit, 0) + "% | " + juce::String (p.space, 0) + "% | "
                      + juce::String (p.tail, 2) + " s | " + ah::formatGateForHost (p.gate) + " | " + juce::String (p.after, 0) + " ms | "
                      + juce::String (p.sens, 0) + "% | " + juce::String (p.tone, 0) + "% | " + juce::String (p.width, 0) + "% | "
                      + (p.sync ? "On" : "Off") + " |");
        }
        out.getParentDirectory().createDirectory();
        if (! out.replaceWithText (rows.joinIntoString ("\n") + "\n")) return 1;
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    int writeIcon (const juce::File& out, int size)
    {
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        const float s = (float) size;
        auto tile = juce::Rectangle<float> (0.0f, 0.0f, s, s).reduced (s * 0.03f);
        g.setColour (ah::ui::colour::surface);
        g.fillRoundedRectangle (tile, s * 0.22f);
        g.setColour (ah::ui::colour::hairline);
        g.drawRoundedRectangle (tile.reduced (s * 0.006f), s * 0.22f, s * 0.012f);
        ah::ui::drawLogoMark (g, tile.reduced (s * 0.24f));
        out.getParentDirectory().createDirectory();
        if (! writePng (img, out)) return 1;
        std::printf ("wrote %s  (%d x %d)\n", out.getFullPathName().toRawUTF8(), size, size);
        return 0;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    if (argc > 2 && juce::String (argv[1]) == "--icon")
        return writeIcon (cwd.getChildFile (argv[2]), 1024);
    if (argc > 2 && juce::String (argv[1]) == "--params")
        return writeParameterTable (cwd.getChildFile (argv[2]));
    return render (cwd.getChildFile (argc > 1 ? argv[1] : "ui-shots"));
}
