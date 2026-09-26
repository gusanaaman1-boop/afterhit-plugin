// Offline renderer for listening checks.
//
//     AfterHitRender in.wav out.wav [preset] [id=value ...]
//
//   preset   a factory program name ("Wide Clap", "tight kick", ...) or index
//   id=value any parameter in plain units, e.g. space=45 tail=2.5 sync=0
//
// Runs the real AudioProcessor over the file in 256-sample blocks (as a host
// would, 120 BPM transport), appends the plug-in's tail, and writes a 24-bit
// WAV at the input's rate. Prints the onset count and levels.

#include <cstdio>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../Core/Presets.h"
#include "../PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3)
    {
        std::fprintf (stderr, "usage: AfterHitRender in.wav out.wav [preset] [id=value ...]\n");
        return 2;
    }
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto inFile = cwd.getChildFile (juce::String::fromUTF8 (argv[1]));
    const auto outFile = cwd.getChildFile (juce::String::fromUTF8 (argv[2]));

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr) { std::fprintf (stderr, "cannot read %s\n", argv[1]); return 1; }

    const double fs = reader->sampleRate;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);

    AfterHitAudioProcessor p;
    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg = juce::String::fromUTF8 (argv[a]);
        if (! arg.containsChar ('='))
        {
            int idx = arg.containsOnly ("0123456789") ? arg.getIntValue() : -1;
            for (int i = 0; i < ah::presets::count() && idx < 0; ++i)
                if (ah::presets::name (i).equalsIgnoreCase (arg)) idx = i;
            if (idx < 0 || idx >= ah::presets::count()) { std::fprintf (stderr, "unknown preset %s\n", argv[a]); return 2; }
            p.applyPreset (idx);
            continue;
        }
        const auto id = arg.upToFirstOccurrenceOf ("=", false, false);
        auto* param = p.getState().getParameter (id);
        if (param == nullptr) { std::fprintf (stderr, "unknown parameter %s\n", id.toRawUTF8()); return 2; }
        param->setValueNotifyingHost (param->convertTo0to1 (arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }

    p.setPlayConfigDetails (channels, channels, fs, 256);
    p.prepareToPlay (fs, 256);

    const auto inLen = (juce::int64) reader->lengthInSamples;
    const auto total = inLen + (juce::int64) (p.getTailLengthSeconds() * fs);
    juce::AudioBuffer<float> all (channels, (int) total);
    all.clear();
    reader->read (&all, 0, (int) inLen, 0, true, channels > 1);
    const float inPeak = all.getMagnitude (0, (int) inLen);

    juce::MidiBuffer midi;
    for (juce::int64 pos = 0; pos < total; pos += 256)
    {
        const int n = (int) juce::jmin ((juce::int64) 256, total - pos);
        juce::AudioBuffer<float> view (all.getArrayOfWritePointers(), channels, (int) pos, n);
        p.processBlock (view, midi);
    }

    outFile.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (outFile.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (fs)
                                                   .withNumChannels (channels).withBitsPerSample (24));
    if (writer == nullptr) { std::fprintf (stderr, "cannot write %s\n", argv[2]); return 1; }
    writer->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    writer.reset();

    std::printf ("%s -> %s  (%.1f kHz, %d ch, %.2f s)  onsets %d  in peak %.1f dBFS  out peak %.1f dBFS\n",
                 inFile.getFileName().toRawUTF8(), outFile.getFileName().toRawUTF8(), fs / 1000.0, channels,
                 (double) total / fs, p.getTelemetry().onsets.load(),
                 juce::Decibels::gainToDecibels (inPeak), juce::Decibels::gainToDecibels (all.getMagnitude (0, all.getNumSamples())));
    return 0;
}
