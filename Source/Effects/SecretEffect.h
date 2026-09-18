#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace texture
{
    /** GRIT: the hidden effect.

        A bit crusher and sample-rate decimator with a wet/dry mix, reachable only
        by finding the right pixel in the interface. It is bypassed completely at
        zero mix, so it costs nothing and changes nothing unless it is found.
    */
    class SecretEffect
    {
    public:
        static constexpr float minBits = 2.0f;
        static constexpr float maxBits = 16.0f;
        static constexpr float defaultBits = 12.0f;

        /** Decimation factor: 1 leaves the rate untouched, 64 is a heavy stutter. */
        static constexpr float minDownsample = 1.0f;
        static constexpr float maxDownsample = 64.0f;
        static constexpr float defaultDownsample = 4.0f;

        static constexpr float defaultMix = 0.0f;

        void prepare (double sampleRate, int numChannels);
        void reset();

        void setBitDepth (float bits) noexcept;
        void setDownsample (float factor) noexcept;
        void setMix (float newMix) noexcept;

        float getBitDepth() const noexcept { return bitDepth; }
        float getDownsample() const noexcept { return downsample; }
        float getMix() const noexcept { return mix; }

        bool isAudible() const noexcept { return mix > 0.0001f; }

        /** Processes in place. Real-time safe; returns immediately when inaudible. */
        void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    private:
        static constexpr int maxChannels = 2;

        float bitDepth { defaultBits };
        float downsample { defaultDownsample };
        float mix { defaultMix };

        float quantisationStep { 1.0f };
        float holdValue[maxChannels] { 0.0f, 0.0f };
        float phase[maxChannels] { 0.0f, 0.0f };
        int preparedChannels { 2 };
    };
}
