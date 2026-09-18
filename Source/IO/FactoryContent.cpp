#include "FactoryContent.h"

#include <cmath>
#include <memory>
#include <vector>

namespace texture
{
    namespace
    {
        constexpr double twoPi = 6.283185307179586476925286766559;

        double midiToFrequency (double midiNote)
        {
            return 440.0 * std::pow (2.0, (midiNote - 69.0) / 12.0);
        }

        /** One sine in the drone, with its own detune, level, pan and slow drift. */
        struct DroneVoice
        {
            double increment;
            double phase;
            double level;
            double leftGain;
            double rightGain;
            double driftRate;
            double driftPhase;
        };

        std::vector<DroneVoice> buildVoices (int index, double rate)
        {
            const auto t = FactoryContent::numSamples > 1
                ? static_cast<double> (index) / (FactoryContent::numSamples - 1)
                : 0.0;

            const auto rootNote = FactoryContent::lowestMidiNote
                                + t * (FactoryContent::highestMidiNote - FactoryContent::lowestMidiNote);

            // A major triad plus the octave: enough harmonic body to granulate well.
            const double intervals[] = { 0.0, 4.0, 7.0, 12.0 };
            const double levels[]    = { 0.50, 0.32, 0.30, 0.18 };
            const double pans[]      = { 0.0, -0.45, 0.45, 0.0 };

            std::vector<DroneVoice> voices;
            voices.reserve (8);

            for (int i = 0; i < 4; ++i)
            {
                // Two detuned copies of each interval give the width that makes these
                // read as pads rather than as test tones.
                for (int side = 0; side < 2; ++side)
                {
                    const auto cents = (side == 0 ? -1.0 : 1.0) * (3.0 + i * 1.5);
                    const auto frequency = midiToFrequency (rootNote + intervals[i])
                                         * std::pow (2.0, cents / 1200.0);

                    const auto pan = juce::jlimit (-1.0, 1.0, pans[i] + (side == 0 ? -0.12 : 0.12));
                    const auto angle = (pan + 1.0) * 0.5 * (twoPi * 0.25);

                    const auto voiceIndex = static_cast<double> (i * 2 + side);

                    voices.push_back ({
                        twoPi * frequency / rate,
                        voiceIndex * 0.7853981634,
                        levels[i] * 0.5,
                        std::cos (angle),
                        std::sin (angle),
                        0.03 + 0.017 * voiceIndex,
                        voiceIndex * 1.31
                    });
                }
            }

            return voices;
        }
    }

    juce::String FactoryContent::getFileName (int index)
    {
        return "Pad_" + juce::String (index + 1).paddedLeft ('0', 2) + ".wav";
    }

    bool FactoryContent::isInstalledIn (const juce::File& directory)
    {
        if (! directory.isDirectory())
            return false;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto file = directory.getChildFile (getFileName (i));

            if (! file.existsAsFile() || file.getSize() <= 0)
                return false;
        }

        return true;
    }

    void FactoryContent::renderPad (juce::AudioBuffer<float>& destination, int index,
                                    double rate, int numFrames)
    {
        destination.setSize (numChannels, numFrames, false, true, true);
        destination.clear();

        if (numFrames <= 0 || rate <= 0.0)
            return;

        auto voices = buildVoices (index, rate);

        auto* left = destination.getWritePointer (0);
        auto* right = destination.getWritePointer (1);

        const auto fadeFrames = juce::jmax (1, static_cast<int> (juce::jmin (1.5, numFrames / rate * 0.2) * rate));

        for (int frame = 0; frame < numFrames; ++frame)
        {
            const auto timeSeconds = static_cast<double> (frame) / rate;

            double sumLeft = 0.0;
            double sumRight = 0.0;

            for (auto& voice : voices)
            {
                // Slow amplitude drift so the drone never sounds static.
                const auto drift = 0.85 + 0.15 * std::sin (twoPi * voice.driftRate * timeSeconds
                                                            + voice.driftPhase);
                const auto value = std::sin (voice.phase) * voice.level * drift;

                sumLeft += value * voice.leftGain;
                sumRight += value * voice.rightGain;

                voice.phase += voice.increment;

                if (voice.phase > twoPi)
                    voice.phase -= twoPi;
            }

            // Fade both ends so the file granulates and wraps without clicks.
            double envelope = 1.0;

            if (frame < fadeFrames)
                envelope = static_cast<double> (frame) / fadeFrames;
            else if (frame > numFrames - fadeFrames)
                envelope = static_cast<double> (numFrames - frame) / fadeFrames;

            envelope = juce::jlimit (0.0, 1.0, envelope);

            left[frame]  = static_cast<float> (juce::jlimit (-1.0, 1.0, sumLeft * envelope * 0.6));
            right[frame] = static_cast<float> (juce::jlimit (-1.0, 1.0, sumRight * envelope * 0.6));
        }
    }

    bool FactoryContent::writeOneFile (const juce::File& destination, int index)
    {
        const auto totalFrames = static_cast<int> (lengthSeconds * sampleRate);

        juce::AudioBuffer<float> buffer;

        try
        {
            renderPad (buffer, index, sampleRate, totalFrames);
        }
        catch (const std::bad_alloc&)
        {
            juce::Logger::writeToLog ("[error] [factory] Out of memory generating " + destination.getFileName());
            return false;
        }

        // Write to a temporary first so an interrupted run never leaves a half file
        // that later looks installed.
        const auto temporary = destination.getSiblingFile (destination.getFileName() + ".partial");
        temporary.deleteFile();

        auto stream = temporary.createOutputStream();

        if (stream == nullptr)
            return false;

        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer (
            format.createWriterFor (stream.get(), sampleRate, numChannels, bitDepth, {}, 0));

        if (writer == nullptr)
            return false;

        stream.release();   // the writer owns it now

        const auto ok = writer->writeFromAudioSampleBuffer (buffer, 0, totalFrames);
        writer->flush();
        writer.reset();

        if (! ok)
        {
            temporary.deleteFile();
            return false;
        }

        return temporary.moveFileTo (destination);
    }

    int FactoryContent::generateInto (const juce::File& directory)
    {
        if (! directory.createDirectory())
            return -1;

        int written = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto file = directory.getChildFile (getFileName (i));

            // Never clobber content that is already there.
            if (file.existsAsFile() && file.getSize() > 0)
                continue;

            if (writeOneFile (file, i))
                ++written;
            else
                juce::Logger::writeToLog ("[warn] [factory] Could not write " + file.getFullPathName());
        }

        return written;
    }
}
