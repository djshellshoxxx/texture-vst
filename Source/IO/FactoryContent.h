#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace texture
{
    /** Generation and installation of the bundled factory samples.

        The twenty factory pads are synthesised rather than shipped. Thirty seconds
        of 48 kHz stereo audio is roughly 5.8 MB per file, so bundling them would
        blow the 15 MB installer budget in 10_CONSTRAINTS.md many times over.
        Generating them on first launch keeps the binary tiny and still gives a
        fresh install twenty playable pads.

        The same code backs the `generate_placeholder_samples` build tool, so what a
        developer gets in `factory_samples/` is byte-identical to what a user gets.
    */
    class FactoryContent
    {
    public:
        static constexpr int numSamples = 20;
        static constexpr double sampleRate = 48000.0;
        static constexpr double lengthSeconds = 30.0;
        static constexpr int bitDepth = 16;
        static constexpr int numChannels = 2;

        /** A2 (MIDI 45) up to G4 (MIDI 67), spread evenly over the twenty files. */
        static constexpr int lowestMidiNote = 45;
        static constexpr int highestMidiNote = 67;

        /** `Pad_01.wav` .. `Pad_20.wav`. */
        static juce::String getFileName (int index);

        /** True when every expected file is present and non-empty. */
        static bool isInstalledIn (const juce::File& directory);

        /** Writes any missing files. Existing files are never overwritten, so real
            samples dropped in under the same names survive. Returns how many were
            written, or -1 if the directory could not be created.
        */
        static int generateInto (const juce::File& directory);

        /** Renders one pad into a buffer. Exposed for the unit tests. */
        static void renderPad (juce::AudioBuffer<float>& destination, int index,
                               double rate, int numFrames);

    private:
        static bool writeOneFile (const juce::File& destination, int index);
    };
}
