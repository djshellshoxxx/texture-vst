#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "ParameterIDs.h"
#include "VersionedRandomStreams.h"
#include "../Presets/LockPolicy.h"

#include <cmath>
#include <string>
#include <vector>

namespace texture
{
    struct StateSchema final
    {
        inline static const juce::Identifier rootTag { "TexturePluginState" };
        inline static const juce::Identifier parametersTag { "Parameters" };
        inline static const juce::Identifier sampleTag { "Sample" };
        inline static const juce::Identifier presetTag { "Preset" };
        inline static const juce::Identifier secretTag { "Grit" };
        inline static const juce::Identifier compareTag { "Compare" };
        inline static const juce::Identifier compareSlotTag { "Slot" };
        inline static const juce::Identifier versionAttribute { "pluginVersion" };
        inline static const juce::Identifier featureStateTag { "FeatureState" };
        inline static const juce::Identifier lockTag { "Lock" };

        /** Optional host-state block for per-instance editing locks (F02). Missing means
            no locks. Newer readers reject unknown versions; this constant is the only
            version this build reads or writes. */
        static constexpr int featureStateSchemaVersion = 1;

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
            invalidSecretData,
            invalidCompareData,
            invalidFeatureData
        };

        struct Result
        {
            Status status { Status::ok };
            juce::String message;

            bool isOk() const noexcept { return status == Status::ok; }
        };

        /** Calls `setDefault` for every registered parameter that `parameterState` does
            not carry. Parameters are matched by their "id" property in the APVTS state.
            Older states predate newer controls; this keeps them from inheriting values
            the instance held before the restore. Pure: takes the callback as a template
            so tests can record the calls. */
        template <typename SetDefault>
        static void applyDefaultsForMissingParameters (const juce::ValueTree& parameterState,
                                                       SetDefault&& setDefault)
        {
            for (const auto* id : params::allIDs)
            {
                bool present = false;

                for (const auto& child : parameterState)
                {
                    if (child.getProperty ("id").toString() == juce::String (id))
                    {
                        present = true;
                        break;
                    }
                }

                if (! present)
                    setDefault (id);
            }
        }

        /** Parameter IDs the state does not carry, in registry order. Test seam for the
            migration rule above. */
        static juce::StringArray missingParameterIDs (const juce::ValueTree& parameterState)
        {
            juce::StringArray missing;
            applyDefaultsForMissingParameters (parameterState,
                                               [&missing] (const char* id) { missing.add (id); });
            return missing;
        }

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

            if (root.hasProperty ("randomSeedV1"))
            {
                const auto seedText = root.getProperty ("randomSeedV1").toString();
                if (seedText.length() != 16 || ! seedText.containsOnly ("0123456789abcdefABCDEF"))
                    return { Status::invalidFeatureData,
                             "Plugin state contains an invalid random seed." };
            }
            if (root.hasProperty ("randomAlgorithmVersion")
                && static_cast<int> (root.getProperty ("randomAlgorithmVersion"))
                    != static_cast<int> (VersionedRandomStreams::algorithmVersion))
                return { Status::invalidFeatureData,
                         "Plugin state uses an unsupported random algorithm version." };

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
            bool seenCompare = false;
            bool seenFeatureState = false;

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

                if (child.getType() == compareTag)
                {
                    if (seenCompare)
                        return { Status::duplicateSingleton,
                                 "Plugin state contains duplicate A/B comparison state." };

                    seenCompare = true;

                    const auto active = static_cast<int> (child.getProperty ("active", 0));
                    if (active < 0 || active >= 2)
                        return { Status::invalidCompareData,
                                 "Plugin state contains an invalid active A/B slot." };

                    bool seenSlots[2] { false, false };

                    for (const auto& slot : child)
                    {
                        if (slot.getType() != compareSlotTag)
                            return { Status::invalidCompareData,
                                     "Plugin state contains malformed A/B slot data." };

                        const auto index = static_cast<int> (slot.getProperty ("index", -1));
                        if (index < 0 || index >= 2 || seenSlots[index])
                            return { Status::invalidCompareData,
                                     "Plugin state contains invalid or duplicate A/B slots." };

                        seenSlots[index] = true;

                        for (int propertyIndex = 0; propertyIndex < slot.getNumProperties(); ++propertyIndex)
                        {
                            const auto name = slot.getPropertyName (propertyIndex).toString();

                            if (name == "index")
                                continue;

                            double value = 0.0;
                            if (! readFiniteNumber (slot.getProperty (slot.getPropertyName (propertyIndex)), value))
                                return { Status::invalidCompareData,
                                         "Plugin state contains non-numeric A/B parameter data." };
                        }
                    }

                    continue;
                }

                if (child.getType() == featureStateTag)
                {
                    if (seenFeatureState)
                        return { Status::duplicateSingleton,
                                 "Plugin state contains duplicate feature state." };

                    seenFeatureState = true;

                    if (child.getProperty ("schema").toString() != juce::String (featureStateSchemaVersion))
                        return { Status::invalidFeatureData,
                                 "Plugin state uses an unsupported feature state version." };

                    std::vector<std::string> lockIds;

                    for (const auto& lock : child)
                    {
                        if (lock.getType() != lockTag)
                            return { Status::invalidFeatureData,
                                     "Plugin state contains malformed lock records." };

                        const auto idValue = lock.getProperty ("id");

                        if (! idValue.isString())
                            return { Status::invalidFeatureData,
                                     "Plugin state contains a lock record with a non-text ID." };

                        lockIds.push_back (idValue.toString().toStdString());
                    }

                    if (! LockPolicy::parseIds (lockIds).accepted)
                        return { Status::invalidFeatureData,
                                 "Plugin state contains an oversized or empty lock list." };

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
