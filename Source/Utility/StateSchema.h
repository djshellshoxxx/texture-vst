#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <cmath>

namespace texture
{
    struct StateSchema final
    {
        inline static const juce::Identifier rootTag { "TexturePluginState" };
        inline static const juce::Identifier parametersTag { "Parameters" };
        inline static const juce::Identifier sampleTag { "Sample" };
        inline static const juce::Identifier presetTag { "Preset" };
        inline static const juce::Identifier secretTag { "Grit" };
        inline static const juce::Identifier versionAttribute { "pluginVersion" };

        static constexpr int currentMajorVersion = 1;

        enum class Status
        {
            ok = 0,
            invalidTree,
            wrongRoot,
            invalidVersion,
            futureVersion,
            missingParameters,
            invalidSampleIndex,
            duplicateSampleIndex,
            invalidSampleData,
            duplicateSingleton,
            invalidSecretData
        };

        struct Result
        {
            Status status { Status::ok };
            juce::String message;

            bool isOk() const noexcept { return status == Status::ok; }
        };

        static Result validate (const juce::ValueTree& root)
        {
            if (! root.isValid())
                return { Status::invalidTree, "Plugin state is not a valid tree." };

            if (root.getType() != rootTag)
                return { Status::wrongRoot, "Plugin state belongs to a different product or schema." };

            const auto rawVersion = root.getProperty (versionAttribute).toString().trim();

            if (rawVersion.isNotEmpty())
            {
                const auto firstDot = rawVersion.indexOfChar ('.');
                const auto majorText = firstDot >= 0 ? rawVersion.substring (0, firstDot)
                                                     : rawVersion;

                if (majorText.isEmpty() || ! majorText.containsOnly ("0123456789"))
                    return { Status::invalidVersion, "Plugin state has an invalid version identifier." };

                const auto major = majorText.getIntValue();

                if (major <= 0)
                    return { Status::invalidVersion, "Plugin state has an invalid version identifier." };

                if (major > currentMajorVersion)
                    return { Status::futureVersion,
                             "Plugin state was written by a newer major version and cannot be applied safely." };
            }

            const auto parameters = root.getChildWithName (parametersTag);

            if (! parameters.isValid() || parameters.getNumChildren() != 1
                || ! parameters.getChild (0).isValid())
            {
                return { Status::missingParameters,
                         "Plugin state is incomplete because its parameter state is missing." };
            }

            bool seenSamples[2] { false, false };
            bool seenPreset = false;
            bool seenSecret = false;

            const auto readFiniteNumber = [] (const juce::var& value, double& result)
            {
                if (! (value.isInt() || value.isInt64() || value.isDouble() || value.isBool()))
                    return false;

                result = static_cast<double> (value);
                return std::isfinite (result);
            };

            for (const auto& child : root)
            {
                if (child.getType() == sampleTag)
                {
                    const auto index = static_cast<int> (child.getProperty ("index", -1));

                    if (index < 0 || index >= 2)
                        return { Status::invalidSampleIndex,
                                 "Plugin state contains an invalid sample layer index." };

                    if (seenSamples[index])
                        return { Status::duplicateSampleIndex,
                                 "Plugin state contains duplicate sample layer records." };

                    seenSamples[index] = true;

                    if (child.hasProperty ("start"))
                    {
                        double start = 0.0;

                        if (! readFiniteNumber (child.getProperty ("start"), start)
                            || start < 0.0 || start > 1.0)
                        {
                            return { Status::invalidSampleData,
                                     "Plugin state contains an invalid sample start position." };
                        }
                    }

                    continue;
                }

                if (child.getType() == presetTag)
                {
                    if (seenPreset)
                        return { Status::duplicateSingleton,
                                 "Plugin state contains duplicate preset metadata." };

                    seenPreset = true;
                    continue;
                }

                if (child.getType() == secretTag)
                {
                    if (seenSecret)
                        return { Status::duplicateSingleton,
                                 "Plugin state contains duplicate GRIT state." };

                    seenSecret = true;

                    const auto validateRange = [&] (const char* property, double fallback,
                                                    double minimum, double maximum)
                    {
                        if (! child.hasProperty (property))
                            return true;

                        double value = fallback;
                        return readFiniteNumber (child.getProperty (property), value)
                            && value >= minimum && value <= maximum;
                    };

                    if (! validateRange ("bits", 12.0, 2.0, 16.0)
                        || ! validateRange ("downsample", 4.0, 1.0, 64.0)
                        || ! validateRange ("mix", 0.0, 0.0, 1.0))
                    {
                        return { Status::invalidSecretData,
                                 "Plugin state contains invalid GRIT settings." };
                    }

                    if (child.hasProperty ("revealed") && ! child.getProperty ("revealed").isBool())
                        return { Status::invalidSecretData,
                                 "Plugin state contains an invalid GRIT visibility flag." };
                }
            }

            return {};
        }
    };
}
