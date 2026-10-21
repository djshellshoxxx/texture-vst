// TEXTURE™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Engine/AuditionChords.h"
#include "Utility/DspHelpers.h"
#include "Utility/StateSchema.h"

#include <cmath>
#include <algorithm>
#include <string>

namespace texture
{
    class RandomSeedChangeAction final : public juce::UndoableAction
    {
    public:
        RandomSeedChangeAction (TextureAudioProcessor& owner, std::uint64_t previous, std::uint64_t next)
            : processor (owner), before (previous), after (next) {}

        bool perform() override { processor.applyRandomSeedFromUndo (after); return true; }
        bool undo() override { processor.applyRandomSeedFromUndo (before); return true; }
        int getSizeInUnits() override { return static_cast<int> (sizeof (*this)); }

    private:
        TextureAudioProcessor& processor;
        std::uint64_t before;
        std::uint64_t after;
    };

    namespace
    {
        /** Morph is ramped over this long so a sweep never zips (06_MORPH_KNOB_BEHAVIOUR.md). */
        constexpr double morphSmoothingSeconds = 0.02;

        /** Header LED reaches -60 dB after this much silence, independent of block size. */
        constexpr double peakDecayToMinusSixtySeconds = 0.37;

        juce::String pluginVersionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "0.0.1";
           #endif
        }

        LogLevel toLogLevel (SampleLoader::Severity severity)
        {
            switch (severity)
            {
                case SampleLoader::Severity::info:    return LogLevel::info;
                case SampleLoader::Severity::warning: return LogLevel::warn;
                case SampleLoader::Severity::error:   return LogLevel::error;
            }

            return LogLevel::info;
        }
    }

    // =========================================================================
    //  Parameter layout
    // =========================================================================

    namespace
    {
        /** Logarithmic travel between two positive limits: equal ratios take equal knob travel. */
        juce::NormalisableRange<float> logarithmicRange (float minimum, float maximum)
        {
            return { minimum, maximum,
                     [] (float start, float end, float normalised) { return start * std::pow (end / start, normalised); },
                     [] (float start, float end, float value) { return std::log (value / start) / std::log (end / start); },
                     [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout TextureAudioProcessor::createParameterLayout()
    {
        using Range = juce::NormalisableRange<float>;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        Range morphRange { params::morphMin, params::morphMax, 0.0001f };

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::morph, 1 },
            params::displayNameFor (params::morph),
            morphRange,
            params::morphDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        // Times use a skewed range so the knob spends most of its travel where pads live.
        Range attackRange { params::attackMinMs, params::attackMaxMs, 0.1f };
        attackRange.setSkewForCentre (params::attackDefaultMs);

        Range decayRange { params::decayMinMs, params::decayMaxMs, 0.1f };
        decayRange.setSkewForCentre (params::decayDefaultMs);

        Range releaseRange { params::releaseMinMs, params::releaseMaxMs, 0.1f };
        releaseRange.setSkewForCentre (params::releaseDefaultMs);

        const auto msFormatter = [] (float value, int)
        {
            return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " s"
                                    : juce::String (juce::roundToInt (value)) + " ms";
        };

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::attack, 1 }, params::displayNameFor (params::attack),
            attackRange, params::attackDefaultMs,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (msFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::decay, 1 }, params::displayNameFor (params::decay),
            decayRange, params::decayDefaultMs,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (msFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::sustain, 1 }, params::displayNameFor (params::sustain),
            Range { params::sustainMin, params::sustainMax, 0.0001f }, params::sustainDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::release, 1 }, params::displayNameFor (params::release),
            releaseRange, params::releaseDefaultMs,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (msFormatter)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::reverbOn, 1 }, params::displayNameFor (params::reverbOn),
            params::reverbOnDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::gainDb, 1 }, params::displayNameFor (params::gainDb),
            Range { params::gainMinDb, params::gainMaxDb, 0.1f }, params::gainDefaultDb,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 1) + " dB"; })));

        // Logarithmic rate: the geometric centre keeps slow rates usable.
        Range lfoRateRange { params::lfoRateMinHz, params::lfoRateMaxHz, 0.001f };
        lfoRateRange.setSkewForCentre (std::sqrt (params::lfoRateMinHz * params::lfoRateMaxHz));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::lfoRate, 1 }, params::displayNameFor (params::lfoRate),
            lfoRateRange, params::lfoRateDefaultHz,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (value, 2) + " Hz"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::lfoDepth, 1 }, params::displayNameFor (params::lfoDepth),
            Range { params::lfoDepthMin, params::lfoDepthMax, 0.0001f }, params::lfoDepthDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::lfoSync, 1 }, params::displayNameFor (params::lfoSync),
            params::lfoSyncDefault));

        juce::StringArray divisionChoices;

        for (const auto* name : params::divisionNames)
            divisionChoices.add (name);

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::lfoDivision, 1 }, params::displayNameFor (params::lfoDivision),
            divisionChoices, params::lfoDivisionDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::layerBlend, 1 }, params::displayNameFor (params::layerBlend),
            Range { params::layerBlendMin, params::layerBlendMax, 0.0001f }, params::layerBlendDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::tune, 1 }, params::displayNameFor (params::tune),
            Range { params::tuneMinSemitones, params::tuneMaxSemitones, 1.0f }, params::tuneDefaultSemitones,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int)
                {
                    const auto semitones = juce::roundToInt (value);
                    return (semitones > 0 ? "+" : "") + juce::String (semitones) + " st";
                })));

        for (const auto* rootId : { params::layer1Root, params::layer2Root })
            layout.add (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { rootId, 1 }, params::displayNameFor (rootId),
                static_cast<int> (params::sourceRootMinMidi), static_cast<int> (params::sourceRootMaxMidi),
                static_cast<int> (params::sourceRootDefaultMidi)));

        const auto centsFormatter = [] (float value, int)
        {
            return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " cents";
        };

        for (const auto* centsId : { params::layer1FineCents, params::layer2FineCents })
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { centsId, 1 }, params::displayNameFor (centsId),
                Range { params::sourceFineMinCents, params::sourceFineMaxCents, 0.1f },
                params::sourceFineDefaultCents,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (centsFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::reverse, 1 }, params::displayNameFor (params::reverse),
            Range { params::reverseMin, params::reverseMax, 0.0001f }, params::reverseDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::shimmer, 1 }, params::displayNameFor (params::shimmer),
            Range { params::shimmerMin, params::shimmerMax, 0.0001f }, params::shimmerDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::randomMode, 1 }, params::displayNameFor (params::randomMode),
            juce::StringArray { "Legacy", "Repeat", "Evolve" }, params::randomModeDefault));

        // --- F07 tone and space. Appended: older states read the Follow MORPH defaults.
        const auto hzFormatter = [] (float value, int)
        {
            return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                    : juce::String (juce::roundToInt (value)) + " Hz";
        };
        const auto percentFormatter = [] (float value, int)
        {
            return juce::String (juce::roundToInt (value * 100.0f)) + "%";
        };

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::toneSpaceMode, 1 }, params::displayNameFor (params::toneSpaceMode),
            juce::StringArray { "Follow MORPH", "Independent" }, params::toneSpaceModeDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneHpHz, 1 }, params::displayNameFor (params::toneHpHz),
            logarithmicRange (params::toneHpMinHz, params::toneHpMaxHz), params::toneHpDefaultHz,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneLpHz, 1 }, params::displayNameFor (params::toneLpHz),
            logarithmicRange (params::toneLpMinHz, params::toneLpMaxHz), params::toneLpDefaultHz,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzFormatter)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::toneFilterOn, 1 }, params::displayNameFor (params::toneFilterOn),
            params::toneFilterOnDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneTiltDb, 1 }, params::displayNameFor (params::toneTiltDb),
            Range { params::toneTiltMinDb, params::toneTiltMaxDb, 0.1f }, params::toneTiltDefaultDb,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
            })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::spaceWidth, 1 }, params::displayNameFor (params::spaceWidth),
            Range { params::spaceWidthMin, params::spaceWidthMax, 0.0001f }, params::spaceWidthDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::spaceReverb, 1 }, params::displayNameFor (params::spaceReverb),
            Range { params::spaceReverbMin, params::spaceReverbMax, 0.0001f }, params::spaceReverbDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::spaceChorus, 1 }, params::displayNameFor (params::spaceChorus),
            Range { params::spaceChorusMin, params::spaceChorusMax, 0.0001f }, params::spaceChorusDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneDrive, 1 }, params::displayNameFor (params::toneDrive),
            Range { params::toneDriveMin, params::toneDriveMax, 0.0001f }, params::toneDriveDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        // --- F11 mono, legato and glide. Appended: older states read Poly with no glide.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::voiceMode, 1 }, params::displayNameFor (params::voiceMode),
            juce::StringArray { "Poly", "Mono Retrigger", "Mono Legato" }, params::voiceModeDefault));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::notePriority, 1 }, params::displayNameFor (params::notePriority),
            juce::StringArray { "Last", "High", "Low" }, params::notePriorityDefault));

        // Glide is logarithmic above zero: a small value is a short slide, and zero is off.
        juce::NormalisableRange<float> glideRange (params::glideMsMin, params::glideMsMax,
            [] (float start, float end, float normalised)
            {
                juce::ignoreUnused (start, end);
                return normalised <= 0.0f ? 0.0f : std::pow (params::glideMsMax + 1.0f, normalised) - 1.0f;
            },
            [] (float start, float end, float value)
            {
                juce::ignoreUnused (start, end);
                return value <= 0.0f ? 0.0f : std::log (value + 1.0f) / std::log (params::glideMsMax + 1.0f);
            },
            [] (float start, float end, float value)
            {
                return juce::jlimit (start, end, value);
            });

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::glideMs, 1 }, params::displayNameFor (params::glideMs),
            glideRange, params::glideMsDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " s"
                                        : juce::String (juce::roundToInt (value)) + " ms";
            })));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::glideMode, 1 }, params::displayNameFor (params::glideMode),
            juce::StringArray { "Always", "Overlap only" }, params::glideModeDefault));

        // --- F13 expandable grain controls. Appended: older states read Follow MORPH.
        // Times are skewed like ADSR so the knob's travel favours short grains.
        Range grainSizeRange { params::grainSizeMinMs, params::grainSizeMaxMs, 0.0001f };
        grainSizeRange.setSkewForCentre (params::grainSizeDefaultMs);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::grainSizeMs, 1 }, params::displayNameFor (params::grainSizeMs),
            grainSizeRange, params::grainSizeDefaultMs,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " ms";
            })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::grainSizeFollow, 1 }, params::displayNameFor (params::grainSizeFollow),
            params::grainFollowDefault));

        Range grainDensityRange { params::grainDensityMinHz, params::grainDensityMaxHz, 0.0001f };
        grainDensityRange.setSkewForCentre (params::grainDensityDefaultHz);

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::grainDensityHz, 1 }, params::displayNameFor (params::grainDensityHz),
            grainDensityRange, params::grainDensityDefaultHz,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (value, 1) + " Hz";
            })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::grainDensityFollow, 1 }, params::displayNameFor (params::grainDensityFollow),
            params::grainFollowDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::grainJitter, 1 }, params::displayNameFor (params::grainJitter),
            Range { params::grainJitterMin, params::grainJitterMax, 0.0001f }, params::grainJitterDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::grainJitterFollow, 1 }, params::displayNameFor (params::grainJitterFollow),
            params::grainFollowDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::grainDetuneCents, 1 }, params::displayNameFor (params::grainDetuneCents),
            Range { params::grainDetuneMinCents, params::grainDetuneMaxCents, 0.0001f }, params::grainDetuneDefaultCents,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " cents";
            })));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::grainDetuneFollow, 1 }, params::displayNameFor (params::grainDetuneFollow),
            params::grainFollowDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::grainPanSpread, 1 }, params::displayNameFor (params::grainPanSpread),
            Range { params::grainPanSpreadMin, params::grainPanSpreadMax, 0.0001f }, params::grainPanSpreadDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::grainPanFollow, 1 }, params::displayNameFor (params::grainPanFollow),
            params::grainFollowDefault));

        // --- F14 modulation shapes. Appended: older states read the original sine LFO.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::lfoShape, 1 }, params::displayNameFor (params::lfoShape),
            juce::StringArray { "Sine", "Triangle", "Step random", "Smooth random", "Curve" }, params::lfoShapeDefault));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::lfoRetrigger, 1 }, params::displayNameFor (params::lfoRetrigger),
            juce::StringArray { "Legacy", "Free", "First note", "Host" }, params::lfoRetriggerDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::lfoPhase, 1 }, params::displayNameFor (params::lfoPhase),
            Range { params::lfoPhaseMin, params::lfoPhaseMax, 0.0001f }, params::lfoPhaseDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

        // --- F10 freeze and scan, one set per layer. Appended: Legacy with Freeze off reads as before.
        const std::array<std::array<const char*, 5>, numLayers> scanLayout {{
            { params::layer1ScanMode, params::layer1ScanPosition, params::layer1ScanSpeed,
              params::layer1Freeze, params::layer1FreezeSpread },
            { params::layer2ScanMode, params::layer2ScanPosition, params::layer2ScanSpeed,
              params::layer2Freeze, params::layer2FreezeSpread } }};

        for (const auto& ids : scanLayout)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { ids[0], 1 }, params::displayNameFor (ids[0]),
                juce::StringArray { "Legacy", "Manual", "Scan" }, params::scanModeDefault));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ids[1], 1 }, params::displayNameFor (ids[1]),
                Range { params::scanPositionMin, params::scanPositionMax, 0.0001f }, params::scanPositionDefault,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ids[2], 1 }, params::displayNameFor (ids[2]),
                Range { params::scanSpeedMin, params::scanSpeedMax, 0.0001f }, params::scanSpeedDefault,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
                {
                    return juce::String (value, 2) + " src/s";
                })));

            layout.add (std::make_unique<juce::AudioParameterBool> (
                juce::ParameterID { ids[3], 1 }, params::displayNameFor (ids[3]), params::freezeDefault));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { ids[4], 1 }, params::displayNameFor (ids[4]),
                Range { params::freezeSpreadMin, params::freezeSpreadMax, 0.0001f }, params::freezeSpreadDefault,
                juce::AudioParameterFloatAttributes().withStringFromValueFunction (percentFormatter)));
        }

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::lfoSlewMs, 1 }, params::displayNameFor (params::lfoSlewMs),
            Range { params::lfoSlewMsMin, params::lfoSlewMsMax, 0.0001f }, params::lfoSlewMsDefault,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int)
            {
                return juce::String (juce::roundToInt (value)) + " ms";
            })));

        return layout;
    }

    // =========================================================================
    //  Construction
    // =========================================================================

    TextureAudioProcessor::StorageInitialiser::StorageInitialiser (juce::ApplicationProperties& properties)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Texture";
        options.filenameSuffix = ".settings";
        options.folderName = "Texture";
        options.osxLibrarySubFolder = "Application Support";
        options.storageFormat = juce::PropertiesFile::storeAsXML;

        properties.setStorageParameters (options);
    }

    TextureAudioProcessor::TextureAudioProcessor()
        : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          storageInitialiser (applicationProperties),
          apvts (*this, &undoManager, "TEXTURE", createParameterLayout()),
          licenseManager (&applicationProperties),
          presetManager (&applicationProperties),
          midiLearn (&applicationProperties)
    {
        Logging::addClient();
        logInfo ("processor", "TEXTURE " + pluginVersionString() + " instantiated.");

        presetManager.installFactoryPresetsIfNeeded();
        presetManager.refresh();

        for (auto& value : pendingControllerValues)
            value.store (-1, std::memory_order_relaxed);

        for (auto& shouldDirty : sampleLoadMarksDirty)
            shouldDirty.store (false, std::memory_order_relaxed);

        sampleLoader.setCallback ([this] (const SampleLoader::LoadResult& result)
        {
            onSampleLoaded (result, result.requestTag);
        });

        cacheParameterPointers();

        // Settings > Preferences > Default output gain applies to every new instance;
        // a restored session overrides it in setStateInformation.
        if (auto* store = applicationProperties.getUserSettings())
            if (store->containsKey ("default_gain_db"))
                setParameterValue (params::gainDb, static_cast<float> (store->getDoubleValue ("default_gain_db", params::gainDefaultDb)));

        renderedAudioCache.setDirectory (getRenderedAudioDirectory());

        enforceTierLimits();

        for (const auto* id : params::allIDs)
            apvts.addParameterListener (id, this);

        startTimerHz (60);

        for (auto& slot : compareSlots)
            slot = captureParameterTree();

        installFactoryContentAsync();
    }

    TextureAudioProcessor::~TextureAudioProcessor()
    {
        stopTimer();

        for (const auto* id : params::allIDs)
            apvts.removeParameterListener (id, this);

        // Shut the loader down before anything it might call back into goes away
        // (19_DEBUGGING.md, common bug 6).
        sampleLoader.setCallback (nullptr);
        sampleLoader.cancelPendingLoads();
        sampleLoader.waitForPendingLoads();
        backgroundPool.removeAllJobs (true, 10000);

        cancelPendingUpdate();

        logInfo ("processor", "TEXTURE instance destroyed.");

        // If crash logging was armed, fold the session log into the crash file so a
        // support ticket only ever needs that one attachment.
        if (crashLogEnabled && crashLogFile != juce::File())
            crashLogFile.appendText (Logging::readLogText());

        Logging::removeClient();
    }

    void TextureAudioProcessor::installFactoryContentAsync()
    {
        const auto directory = PresetManager::getFactorySampleDirectory();

        if (FactoryContent::isInstalledIn (directory))
        {
            factoryInstallFinished.store (true, std::memory_order_release);
            return;
        }

        logInfo ("factory", "Generating factory samples into " + directory.getFullPathName());

        // The destructor waits for this pool, so triggerAsyncUpdate from inside the
        // job can never outlive the processor.
        backgroundPool.addJob ([this, directory]
        {
            const auto written = FactoryContent::generateInto (directory);

            factorySamplesWritten.store (written, std::memory_order_release);
            factoryInstallFinished.store (true, std::memory_order_release);

            triggerAsyncUpdate();
        });
    }

    void TextureAudioProcessor::cacheParameterPointers()
    {
        morphParameter       = apvts.getRawParameterValue (params::morph);
        attackParameter      = apvts.getRawParameterValue (params::attack);
        decayParameter       = apvts.getRawParameterValue (params::decay);
        sustainParameter     = apvts.getRawParameterValue (params::sustain);
        releaseParameter     = apvts.getRawParameterValue (params::release);
        reverbOnParameter    = apvts.getRawParameterValue (params::reverbOn);
        gainParameter        = apvts.getRawParameterValue (params::gainDb);
        lfoRateParameter     = apvts.getRawParameterValue (params::lfoRate);
        lfoDepthParameter    = apvts.getRawParameterValue (params::lfoDepth);
        lfoSyncParameter     = apvts.getRawParameterValue (params::lfoSync);
        lfoDivisionParameter = apvts.getRawParameterValue (params::lfoDivision);
        layerBlendParameter  = apvts.getRawParameterValue (params::layerBlend);
        tuneParameter        = apvts.getRawParameterValue (params::tune);
        sourceRootParameters = { apvts.getRawParameterValue (params::layer1Root),
                                 apvts.getRawParameterValue (params::layer2Root) };
        sourceFineParameters = { apvts.getRawParameterValue (params::layer1FineCents),
                                 apvts.getRawParameterValue (params::layer2FineCents) };
        reverseParameter     = apvts.getRawParameterValue (params::reverse);
        shimmerParameter     = apvts.getRawParameterValue (params::shimmer);
        randomModeParameter  = apvts.getRawParameterValue (params::randomMode);

        toneSpaceParameters.mode     = apvts.getRawParameterValue (params::toneSpaceMode);
        toneSpaceParameters.hp       = apvts.getRawParameterValue (params::toneHpHz);
        toneSpaceParameters.lp       = apvts.getRawParameterValue (params::toneLpHz);
        toneSpaceParameters.filterOn = apvts.getRawParameterValue (params::toneFilterOn);
        toneSpaceParameters.tilt     = apvts.getRawParameterValue (params::toneTiltDb);
        toneSpaceParameters.width    = apvts.getRawParameterValue (params::spaceWidth);
        toneSpaceParameters.reverb   = apvts.getRawParameterValue (params::spaceReverb);
        toneSpaceParameters.chorus   = apvts.getRawParameterValue (params::spaceChorus);
        toneSpaceParameters.drive    = apvts.getRawParameterValue (params::toneDrive);

        performanceParameters.voiceMode    = apvts.getRawParameterValue (params::voiceMode);
        performanceParameters.priority     = apvts.getRawParameterValue (params::notePriority);
        performanceParameters.glideMs      = apvts.getRawParameterValue (params::glideMs);
        performanceParameters.glideMode    = apvts.getRawParameterValue (params::glideMode);

        for (size_t i = 0; i < numGrainFields; ++i)
        {
            grainParameters.values[i] = apvts.getRawParameterValue (grainValueParameterID (static_cast<GrainField> (i)));
            grainParameters.follow[i] = apvts.getRawParameterValue (grainFollowParameterID (static_cast<GrainField> (i)));
        }

        const std::array<std::array<const char*, 5>, numLayers> scanIDs {{
            { params::layer1ScanMode, params::layer1ScanPosition, params::layer1ScanSpeed,
              params::layer1Freeze, params::layer1FreezeSpread },
            { params::layer2ScanMode, params::layer2ScanPosition, params::layer2ScanSpeed,
              params::layer2Freeze, params::layer2FreezeSpread } }};

        for (size_t layer = 0; layer < static_cast<size_t> (numLayers); ++layer)
        {
            scanParameters[layer].mode     = apvts.getRawParameterValue (scanIDs[layer][0]);
            scanParameters[layer].position = apvts.getRawParameterValue (scanIDs[layer][1]);
            scanParameters[layer].speed    = apvts.getRawParameterValue (scanIDs[layer][2]);
            scanParameters[layer].freeze   = apvts.getRawParameterValue (scanIDs[layer][3]);
            scanParameters[layer].spread   = apvts.getRawParameterValue (scanIDs[layer][4]);
        }

        modulationParameters.shape     = apvts.getRawParameterValue (params::lfoShape);
        modulationParameters.retrigger = apvts.getRawParameterValue (params::lfoRetrigger);
        modulationParameters.phase     = apvts.getRawParameterValue (params::lfoPhase);
        modulationParameters.slewMs    = apvts.getRawParameterValue (params::lfoSlewMs);
    }

    ScanSettings TextureAudioProcessor::readScanSettings (int layerIndex) const noexcept
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        const auto& p = scanParameters[index];

        const auto read = [] (const std::atomic<float>* value, float fallback)
        {
            return value != nullptr ? value->load (std::memory_order_relaxed) : fallback;
        };

        ScanSettings settings;
        settings.mode = scanModeFromIndex (juce::roundToInt (read (p.mode, 0.0f)));
        settings.position = read (p.position, params::scanPositionDefault);
        settings.speed = read (p.speed, params::scanSpeedDefault);
        settings.freeze = read (p.freeze, 0.0f) > 0.5f;
        settings.freezeSpread = read (p.spread, params::freezeSpreadDefault);
        return sanitiseScanSettings (settings);
    }

    ScanSettings TextureAudioProcessor::getScanSettings (int layerIndex) const
    {
        return readScanSettings (layerIndex);
    }

    GrainControlSettings TextureAudioProcessor::getGrainControlSettings() const
    {
        GrainControlSettings settings;

        for (size_t i = 0; i < numGrainFields; ++i)
        {
            if (const auto* value = grainParameters.values[i])
                settings.values[i] = value->load (std::memory_order_relaxed);

            if (const auto* follow = grainParameters.follow[i])
                settings.follow[i] = follow->load (std::memory_order_relaxed) > 0.5f;
        }

        return settings;
    }

    float TextureAudioProcessor::getGrainEffectiveValue (GrainField field) const
    {
        const auto index = static_cast<size_t> (field);

        if (grainEffectivePublished.load (std::memory_order_relaxed))
            return grainEffective[index].load (std::memory_order_relaxed);

        // No block has rendered yet (no audio device, or a plugin that is not running). The
        // value is what the next block will show for the current MORPH setting.
        const auto morph = getParameterValue (params::morph, params::morphDefault);
        return GrainTargetResolver::resolve (deriveMorphTargets (morph), getGrainControlSettings())
            .effective[index];
    }

    // =========================================================================
    //  Modulation shapes (F14)
    // =========================================================================

    /** Undo for the modulation curve. Like the other feature undo steps, it applies a whole
        state, so redo and undo always put back exactly the curve that was heard. */
    class TextureAudioProcessor::ModulationCurveAction final : public juce::UndoableAction
    {
    public:
        ModulationCurveAction (TextureAudioProcessor& host, ModulationCurve beforeCurve, ModulationCurve afterCurve)
            : owner (host), before (beforeCurve), after (afterCurve)
        {
        }

        bool perform() override { return owner.applyModulationCurve (after); }
        bool undo() override { return owner.applyModulationCurve (before); }
        int getSizeInUnits() override { return static_cast<int> (sizeof (*this)); }

    private:
        TextureAudioProcessor& owner;
        ModulationCurve before;
        ModulationCurve after;
    };

    bool TextureAudioProcessor::applyModulationCurve (const ModulationCurve& curve)
    {
        if (! curve.isValid())
            return false;

        lfoCurve = curve;
        lfoCurveMailbox.publish (curve);
        return true;
    }

    bool TextureAudioProcessor::setModulationCurve (const ModulationCurve& curve, const juce::String& undoName)
    {
        if (! curve.isValid())
        {
            postToast ("That curve is not valid, so nothing changed.", SampleLoader::Severity::warning);
            return false;
        }

        if (curve == lfoCurve)
            return true;

        const auto previous = lfoCurve;
        undoManager.beginNewTransaction (undoName);
        undoManager.perform (new ModulationCurveAction (*this, previous, curve));
        markPresetDirty();
        return true;
    }

    void TextureAudioProcessor::setGrainFollow (GrainField field, bool shouldFollow)
    {
        // Captured before the transaction: the value the sound is using right now.
        const auto change = planGrainFollowChange (field, shouldFollow, getGrainEffectiveValue (field));

        undoManager.beginNewTransaction (shouldFollow ? "Follow MORPH for grain control"
                                                      : "Set grain control manually");

        for (int i = 0; i < change.count; ++i)
            setParameterValue (change.writes[static_cast<size_t> (i)].parameterID,
                               change.writes[static_cast<size_t> (i)].value);

        markPresetDirty();
    }

    GrainControlSettings TextureAudioProcessor::readGrainSettings() const noexcept
    {
        return getGrainControlSettings();
    }

    MonoConfig TextureAudioProcessor::readPerformanceSettings() const noexcept
    {
        const auto read = [] (const std::atomic<float>* value, float fallback)
        {
            return value != nullptr ? value->load (std::memory_order_relaxed) : fallback;
        };

        const auto& p = performanceParameters;
        return monoConfigFromParameters (juce::roundToInt (read (p.voiceMode, 0.0f)),
                                         juce::roundToInt (read (p.priority, 0.0f)),
                                         read (p.glideMs, params::glideMsDefault),
                                         juce::roundToInt (read (p.glideMode, static_cast<float> (params::glideModeDefault))));
    }

    float TextureAudioProcessor::getFollowMorphValueForGui (int index) const noexcept
    {
        return followMorphValues[static_cast<size_t> (juce::jlimit (0, 3, index))].load (std::memory_order_relaxed);
    }

    void TextureAudioProcessor::startIndependentFromCurrentSound()
    {
        const auto drive = getFollowMorphValueForGui (0);
        const auto chorus = getFollowMorphValueForGui (1);
        const auto reverb = getFollowMorphValueForGui (2);
        const auto tilt = getFollowMorphValueForGui (3);

        // Inverse of the independent mappings in ToneSpaceRouter::resolve, so the first
        // Independent block renders the same drive, chorus, reverb and tilt.
        undoManager.beginNewTransaction ("Start tone and space from current sound");
        setParameterValue (params::toneSpaceMode, static_cast<float> (params::toneSpaceModeIndependent));
        setParameterValue (params::toneDrive, juce::jlimit (params::toneDriveMin, params::toneDriveMax,
                                                            (drive - 0.1f) / 0.25f));
        setParameterValue (params::spaceChorus, juce::jlimit (params::spaceChorusMin, params::spaceChorusMax,
                                                              chorus / 0.6f));
        setParameterValue (params::spaceReverb, juce::jlimit (params::spaceReverbMin, params::spaceReverbMax, reverb));
        setParameterValue (params::toneTiltDb, juce::jlimit (params::toneTiltMinDb, params::toneTiltMaxDb, tilt));
        setParameterValue (params::spaceWidth, params::spaceWidthDefault);
        markPresetDirty();
        postToast ("Tone and space now start from the current sound.", SampleLoader::Severity::info);
    }

    ToneSpaceSettings TextureAudioProcessor::readToneSpaceSettings() const noexcept
    {
        const auto read = [] (const std::atomic<float>* value, float fallback)
        {
            return value != nullptr ? value->load (std::memory_order_relaxed) : fallback;
        };

        const auto& p = toneSpaceParameters;
        ToneSpaceSettings settings;
        settings.independent = read (p.mode, static_cast<float> (params::toneSpaceModeDefault))
                               > 0.5f * static_cast<float> (params::toneSpaceModeIndependent);
        settings.hpHz = read (p.hp, params::toneHpDefaultHz);
        settings.lpHz = read (p.lp, params::toneLpDefaultHz);
        settings.filterOn = read (p.filterOn, 0.0f) > 0.5f;
        settings.tiltDb = read (p.tilt, params::toneTiltDefaultDb);
        settings.width = read (p.width, params::spaceWidthDefault);
        settings.reverbSend = read (p.reverb, params::spaceReverbDefault);
        settings.chorusAmount = read (p.chorus, params::spaceChorusDefault);
        settings.drive = read (p.drive, params::toneDriveDefault);
        return sanitiseToneSpace (settings);
    }

    const juce::String TextureAudioProcessor::getName() const
    {
       #if defined (JucePlugin_Name)
        return JucePlugin_Name;
       #else
        return "Texture";
       #endif
    }

    double TextureAudioProcessor::getTailLengthSeconds() const
    {
        // Longest release plus the reverb tail, so hosts render the full decay.
        return static_cast<double> (params::releaseMaxMs) * 0.001 + 6.0;
    }

    // =========================================================================
    //  Preparation
    // =========================================================================

    void TextureAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        const auto sampleRateChanged = ! juce::approximatelyEqual (sampleRate, currentSampleRate);

        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        currentBlockSize = juce::jmax (1, samplesPerBlock);

        stereoBus.setSize (2, currentBlockSize, false, true, true);
        layerMix.setSize (2, currentBlockSize, false, true, true);

        for (auto& engine : engines)
            engine.prepare (currentSampleRate, currentBlockSize);

        effects.prepare (currentSampleRate, currentBlockSize, 2);
        morphLfo.prepare (currentSampleRate);
        grainResolver.prepare (currentSampleRate);

        smoothedMorph.reset (currentSampleRate, morphSmoothingSeconds);
        smoothedMorph.setCurrentAndTargetValue (morphParameter != nullptr
                                                    ? morphParameter->load()
                                                    : params::morphDefault);

        sampleLoader.prepare (currentSampleRate);

        isPrepared = true;

        logInfo ("processor", "prepareToPlay: " + juce::String (currentSampleRate, 0) + " Hz, "
                            + juce::String (currentBlockSize) + " samples.");

        // Ableton calls prepareToPlay on every transport start; only rebuild the
        // sample buffers when the rate actually moved (19_DEBUGGING.md, host quirks).
        if (sampleRateChanged)
            reloadSamplesForNewSampleRate();
    }

    void TextureAudioProcessor::releaseResources()
    {
        stopAudition();

        for (auto& engine : engines)
            engine.releaseResources();

        effects.releaseResources();
        stereoBus.setSize (0, 0);
        layerMix.setSize (0, 0);
        isPrepared = false;

        // Nothing can be reading a retired buffer once the audio thread has stopped.
        for (auto& layer : layers)
            releaseRetiredIfSafe (layer);

        logInfo ("processor", "releaseResources");
    }

    void TextureAudioProcessor::reloadSamplesForNewSampleRate()
    {
        for (int i = 0; i < numLayers; ++i)
        {
            const auto file = layers[static_cast<size_t> (i)].file;

            if (file.existsAsFile())
            {
                sampleLoadMarksDirty[static_cast<size_t> (i)].store (false, std::memory_order_release);
                sampleLoader.loadAsync (file, i);
            }
        }
    }

    bool TextureAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& output = layouts.getMainOutputChannelSet();

        if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
            return false;

        return layouts.getMainInputChannelSet().isDisabled();
    }

    // =========================================================================
    //  Audio
    // =========================================================================

    void TextureAudioProcessor::updatePerBlockParameters (int numSamples)
    {
        // Read the smoothed MORPH once per block and derive everything from it.
        const auto automatedMorph = morphParameter != nullptr ? morphParameter->load() : params::morphDefault;
        smoothedMorph.setTargetValue (juce::jlimit (0.0f, 1.0f, automatedMorph));

        auto morphNow = smoothedMorph.getNextValue();

        if (numSamples > 1)
            smoothedMorph.skip (numSamples - 1);

        // LFO (Pro): rate, depth and sync come from the parameter tree.
        if (lfoRateParameter != nullptr)
            morphLfo.setRateHz (lfoRateParameter->load());

        if (lfoSyncParameter != nullptr)
            morphLfo.setSyncEnabled (lfoSyncParameter->load() > 0.5f);

        if (lfoDivisionParameter != nullptr)
            morphLfo.setDivisionIndex (juce::roundToInt (lfoDivisionParameter->load()));

        const auto requestedDepth = lfoDepthParameter != nullptr ? lfoDepthParameter->load() : 0.0f;
        morphLfo.setDepth (licenseManager.isPro() ? requestedDepth : 0.0f);

        double hostBpm = 0.0;
        std::optional<double> hostPpq;
        bool hostIsPlaying = false;

        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto position = hostPlayHead->getPosition())
            {
                hostBpm = position->getBpm().orFallback (0.0);

                if (const auto ppq = position->getPpqPosition())
                    hostPpq = *ppq;

                hostIsPlaying = position->getIsPlaying();
            }
        }

        if (hostBpm > 0.0 && std::isfinite (hostBpm))
            lastHostBpm.store (hostBpm, std::memory_order_relaxed);

        // F14 modulation shapes. Read once per block, like every other LFO setting.
        const auto readModulation = [] (const std::atomic<float>* value, float fallback)
        {
            return value != nullptr ? value->load (std::memory_order_relaxed) : fallback;
        };

        morphLfo.setShape (lfoShapeFromIndex (juce::roundToInt (
            readModulation (modulationParameters.shape, static_cast<float> (params::lfoShapeDefault)))));
        morphLfo.setRetrigger (lfoRetriggerFromIndex (juce::roundToInt (
            readModulation (modulationParameters.retrigger, static_cast<float> (params::lfoRetriggerDefault)))));
        morphLfo.setPhaseOffset (readModulation (modulationParameters.phase, params::lfoPhaseDefault));
        morphLfo.setSlewMs (readModulation (modulationParameters.slewMs, params::lfoSlewMsDefault));
        morphLfo.setSeed (randomSeed.load (std::memory_order_relaxed));

        if (lfoCurveMailbox.tryRead (lfoCurveAudio))
            morphLfo.setCurve (lfoCurveAudio);

        morphLfo.updateFromPlayHead (hostBpm, hostIsPlaying, hostPpq);
        morphLfo.advanceBlock (numSamples);
        morphNow = morphLfo.applyTo (morphNow);

        effectiveMorph.store (morphNow, std::memory_order_relaxed);

        currentTargets = deriveMorphTargets (morphNow);
        currentTargets.tuneSemitones = tuneParameter    != nullptr ? tuneParameter->load()    : params::tuneDefaultSemitones;
        currentTargets.reverseChance = reverseParameter != nullptr ? reverseParameter->load() : params::reverseDefault;
        currentTargets.shimmerAmount = shimmerParameter != nullptr ? shimmerParameter->load() : params::shimmerDefault;

        followMorphValues[0].store (currentTargets.saturatorDrive, std::memory_order_relaxed);
        followMorphValues[1].store (currentTargets.chorusDepth, std::memory_order_relaxed);
        followMorphValues[2].store (currentTargets.reverbSend, std::memory_order_relaxed);
        followMorphValues[3].store (currentTargets.highShelfTiltDb, std::memory_order_relaxed);

        // F13: manual grain values and Follow resolve here, once per block, into the targets
        // that every layer and the effects read.
        const auto grain = grainResolver.advance (currentTargets, readGrainSettings(), numSamples);
        currentTargets = grain.targets;

        for (size_t i = 0; i < numGrainFields; ++i)
            grainEffective[i].store (grain.effective[i], std::memory_order_relaxed);

        grainOversubscribedFlag.store (grain.oversubscribed, std::memory_order_relaxed);
        grainEffectivePublished.store (true, std::memory_order_relaxed);

        for (int layer = 0; layer < numLayers; ++layer)
        {
            const auto index = static_cast<size_t> (layer);
            const auto root = sourceRootParameters[index] != nullptr
                                  ? juce::roundToInt (sourceRootParameters[index]->load())
                                  : static_cast<int> (params::sourceRootDefaultMidi);
            const auto fine = sourceFineParameters[index] != nullptr
                                  ? sourceFineParameters[index]->load()
                                  : params::sourceFineDefaultCents;
            engines[index].setSourcePitchCalibration (root, fine);
        }

        const auto attack  = attackParameter  != nullptr ? attackParameter->load()  : params::attackDefaultMs;
        const auto decay   = decayParameter   != nullptr ? decayParameter->load()   : params::decayDefaultMs;
        const auto sustain = sustainParameter != nullptr ? sustainParameter->load() : params::sustainDefault;
        const auto release = releaseParameter != nullptr ? releaseParameter->load() : params::releaseDefaultMs;

        const auto randomMode = randomModeParameter != nullptr
                                    ? juce::roundToInt (randomModeParameter->load())
                                    : params::randomModeDefault;
        const auto seed = randomSeed.load (std::memory_order_relaxed);
        if (randomEvolutionResetRequested.exchange (false, std::memory_order_acq_rel))
            for (auto& engine : engines)
                engine.resetEvolution();
        for (size_t i = 0; i < engines.size(); ++i)
        {
            const auto layerSeed = seed ^ (static_cast<std::uint64_t> (i + 1)
                                           * UINT64_C (0x9E3779B97F4A7C15));
            engines[i].setRandomConfig (randomMode, layerSeed);
            engines[i].setAdsrParameters (attack, decay, sustain, release);
        }

        const auto monoConfig = readPerformanceSettings();
        for (auto& engine : engines)
            engine.setVoiceConfig (monoConfig);

        for (size_t layer = 0; layer < engines.size(); ++layer)
            engines[layer].setScanSettings (readScanSettings (static_cast<int> (layer)));

        effects.setToneSpace (readToneSpaceSettings());
        effects.setMorphTargets (currentTargets);
        effects.getSecretEffect().setBitDepth (secretBitDepth.load (std::memory_order_relaxed));
        effects.getSecretEffect().setDownsample (secretDownsample.load (std::memory_order_relaxed));
        effects.getSecretEffect().setMix (secretMix.load (std::memory_order_relaxed));
        effects.setReverbEnabled (reverbOnParameter == nullptr || reverbOnParameter->load() > 0.5f);
        effects.setOutputGainDb (gainParameter != nullptr ? gainParameter->load() : params::gainDefaultDb);

        // Layer blend: 0 is layer 1 only, 1 is an equal-power pair.
        const auto requestedBlend = layerBlendParameter != nullptr ? layerBlendParameter->load() : 0.0f;
        const auto blend = licenseManager.isPro() ? juce::jlimit (0.0f, 1.0f, requestedBlend) : 0.0f;
        const auto normaliser = 1.0f / std::sqrt (1.0f + blend);

        layerGains[0] = normaliser;
        layerGains[1] = blend * normaliser;
    }

    void TextureAudioProcessor::renderLayers (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        if (numSamples <= 0 || layerMix.getNumSamples() < numSamples)
            return;

        for (int i = 0; i < numLayers; ++i)
        {
            const auto gain = layerGains[static_cast<size_t> (i)];

            if (gain <= 0.0001f)
                continue;

            const auto* source = layers[static_cast<size_t> (i)].live.load (std::memory_order_acquire);

            if (source == nullptr || ! source->isValid())
                continue;

            layerMix.clear (0, numSamples);

            auto layerTargets = currentTargets;
            const auto index = static_cast<size_t> (i);
            layerTargets.sourceFineCents = sourceFineParameters[index] != nullptr
                                               ? sourceFineParameters[index]->load()
                                               : params::sourceFineDefaultCents;

            if (engines[index].renderNextBlock (source, layerTargets, layerMix, 0, numSamples) == 0)
                continue;

            for (int ch = 0; ch < 2; ++ch)
                buffer.addFrom (ch, startSample, layerMix, ch, 0, numSamples, gain);
        }
    }

    void TextureAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
    {
        juce::ScopedNoDenormals noDenormals;

        const auto blockStartTicks = juce::Time::getHighResolutionTicks();
        const auto numSamples = buffer.getNumSamples();
        const auto numOutputChannels = buffer.getNumChannels();

        for (int ch = 0; ch < numOutputChannels; ++ch)
            buffer.clear (ch, 0, numSamples);

        if (! isPrepared || numSamples <= 0 || numOutputChannels <= 0)
            return;

        if (stereoBus.getNumSamples() < numSamples || layerMix.getNumSamples() < numSamples)
            return;   // a host changed the block size without calling prepareToPlay

        updatePerBlockParameters (numSamples);
        applyAuditionCommand();

        stereoBus.clear (0, numSamples);

        // Split the block on MIDI event boundaries so note timing stays sample-accurate.
        int position = 0;

        for (const auto metadata : midiMessages)
        {
            const auto eventTime = juce::jlimit (0, numSamples, metadata.samplePosition);
            const auto segment = eventTime - position;

            if (segment > 0)
            {
                renderLayers (stereoBus, position, segment);
                position = eventTime;
            }

            handleMidiEvent (metadata.getMessage());
        }

        if (position < numSamples)
            renderLayers (stereoBus, position, numSamples - position);

        int activeVoices = 0;

        for (const auto& engine : engines)
            activeVoices += engine.getActiveVoiceCount();

        // Idle optimisation: skip the effects chain only when nothing is sounding and
        // the reverb tail has decayed (07_EFFECTS_CHAIN.md, 14_MIDI_SPEC.md).
        if (activeVoices > 0 || effects.hasTailEnergy())
            effects.process (stereoBus, 0, numSamples);

        if (dsp::sanitiseBuffer (stereoBus))
            nonFiniteDetected.store (true, std::memory_order_relaxed);

        // Fold the internal stereo bus onto whatever the host gave us.
        if (numOutputChannels >= 2)
        {
            buffer.copyFrom (0, 0, stereoBus, 0, 0, numSamples);
            buffer.copyFrom (1, 0, stereoBus, 1, 0, numSamples);
        }
        else
        {
            buffer.copyFrom (0, 0, stereoBus.getReadPointer (0), numSamples, 0.5f);
            buffer.addFrom (0, 0, stereoBus.getReadPointer (1), numSamples, 0.5f);
        }

        // Metering.
        float peak = 0.0f;

        for (int ch = 0; ch < juce::jmin (2, numOutputChannels); ++ch)
        {
            const auto channelPeak = buffer.getMagnitude (ch, 0, numSamples);
            channelPeaks[static_cast<size_t> (ch)].push (channelPeak);
            peak = juce::jmax (peak, channelPeak);
        }

        if (numOutputChannels == 1)
            channelPeaks[1].push (peak);

        outputPeak.push (peak);

        const auto previousSmoothed = smoothedPeak.load (std::memory_order_relaxed);
        const auto peakDecay = static_cast<float> (dsp::decayCoefficient (
            static_cast<double> (numSamples) / currentSampleRate,
            peakDecayToMinusSixtySeconds,
            0.001));
        smoothedPeak.store (juce::jmax (peak, previousSmoothed * peakDecay), std::memory_order_relaxed);

        const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - blockStartTicks);
        const auto blockSeconds = static_cast<double> (numSamples) / currentSampleRate;
        const auto load = blockSeconds > 0.0 ? elapsedSeconds / blockSeconds : 0.0;

        cpuLoad.store (load, std::memory_order_relaxed);

        if (load > peakCpuLoad.load (std::memory_order_relaxed))
            peakCpuLoad.store (load, std::memory_order_relaxed);

        // Last: every read of a layer's live pointer this block happens-before the
        // message thread can observe the new count, which is what makes retiring a
        // swapped-out buffer safe.
        blocksProcessed.fetch_add (1, std::memory_order_acq_rel);
    }

    void TextureAudioProcessor::applyAuditionCommand() noexcept
    {
        const auto command = auditionCommand.load (std::memory_order_acquire);

        if (command == audioAuditionCommand)
            return;

        // Release the prior owned set first. This is deliberately independent of
        // the host MIDI channel and cannot release a physical key of the same pitch.
        for (int note = 0; note <= 127; ++note)
        {
            if (! audioAuditionNotes[static_cast<size_t> (note)])
                continue;

            for (auto& engine : engines)
                engine.noteOff (1, note, true);

            audioAuditionNotes[static_cast<size_t> (note)] = false;
        }

        audioAuditionCommand = command;
        const auto enabled = (command & (1u << 16)) != 0;

        if (! enabled)
            return;

        const auto root = static_cast<int> (command & 0x7fu);
        const auto velocity = static_cast<float> ((command >> 7) & 0x7fu) / 127.0f;
        const auto chord = static_cast<int> ((command >> 14) & 0x3u);
        for (const auto offset : AuditionChords::offsets[static_cast<size_t> (chord)])
        {
            if (offset < 0 || root + offset > 127)
                continue;

            const auto note = root + offset;

            for (auto& engine : engines)
                engine.noteOn (1, note, velocity, true);

            audioAuditionNotes[static_cast<size_t> (note)] = true;
        }
    }

    void TextureAudioProcessor::setAuditionNoteSet (int rootNote, int velocity, int chord, bool enabled) noexcept
    {
        const auto root = static_cast<std::uint32_t> (juce::jlimit (0, 127, rootNote));
        const auto clampedVelocity = static_cast<std::uint32_t> (juce::jlimit (1, 127, velocity));
        const auto clampedChord = static_cast<std::uint32_t> (juce::jlimit (0, 3, chord));
        const auto command = root | (clampedVelocity << 7) | (clampedChord << 14)
                            | (enabled ? (1u << 16) : 0u);
        auditionCommand.store (command, std::memory_order_release);
    }

    bool TextureAudioProcessor::isAuditionRequested() const noexcept
    {
        return (auditionCommand.load (std::memory_order_acquire) & (1u << 16)) != 0;
    }

    void TextureAudioProcessor::handleMidiEvent (const juce::MidiMessage& message)
    {
        if (message.isNoteOn())
        {
            lastMidiNote.store (message.getNoteNumber(), std::memory_order_relaxed);
            lastMidiVelocity.store (message.getVelocity(), std::memory_order_relaxed);

            // The first key of a chord restarts the LFO in First note mode; the rest do not.
            if (heldNotes.noteOn (message.getChannel(), message.getNoteNumber()))
                morphLfo.noteStarted();

            for (auto& engine : engines)
                engine.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff())
        {
            heldNotes.noteOff (message.getChannel(), message.getNoteNumber());

            for (auto& engine : engines)
                engine.noteOff (message.getChannel(), message.getNoteNumber());
        }
        else if (message.isPitchWheel())
        {
            for (auto& engine : engines)
                engine.setPitchBend (message.getPitchWheelValue());
        }
        else if (message.isSustainPedalOn() || message.isSustainPedalOff())
        {
            for (auto& engine : engines)
                engine.setSustainPedal (message.getChannel(), message.isSustainPedalOn());
        }
        else if (message.isAllNotesOff())
        {
            stopAudition();
            heldNotes.clearChannel (message.getChannel());

            for (auto& engine : engines)
                engine.allNotesOff (message.getChannel());
        }
        else if (message.isAllSoundOff())
        {
            stopAudition();
            heldNotes.clearChannel (message.getChannel());

            for (auto& engine : engines)
                engine.allSoundOff (message.getChannel());

            effects.clearTails();
        }
        else if (message.isController())
        {
            const auto controller = message.getControllerNumber();
            const auto value = message.getControllerValue();

            lastMidiController.store (controller, std::memory_order_relaxed);
            lastMidiControllerValue.store (value, std::memory_order_relaxed);

            if (midiLearn.isLearning())
            {
                midiLearn.requestBinding (controller);
                midiLearnBindingPending.store (true, std::memory_order_release);
            }
            else if (midiLearn.getMappedSlotForController (controller) >= 0)
            {
                // Parameters are never written from the audio thread; hand the value
                // to the message thread instead (21_WHAT_NOT_TO_DO.md, item 5).
                pendingControllerValues[static_cast<size_t> (controller)]
                    .store (value, std::memory_order_release);
                hasPendingControllerValues.store (true, std::memory_order_release);
            }
        }
    }

    // =========================================================================
    //  Message-thread work queued from the audio thread
    // =========================================================================

    void TextureAudioProcessor::parameterChanged (const juce::String&, float)
    {
        parameterRevision.fetch_add (1, std::memory_order_relaxed);

        if (suppressPresetDirtyTracking.load (std::memory_order_relaxed))
            return;

        presetDirtyPending.store (true, std::memory_order_release);
    }

    void TextureAudioProcessor::timerCallback()
    {
        // Message-thread poll for work latched by real-time callbacks. Avoid calling
        // AsyncUpdater::triggerAsyncUpdate() from processBlock/parameterChanged.
        if (presetDirtyPending.load (std::memory_order_acquire)
            || midiLearnBindingPending.load (std::memory_order_acquire)
            || hasPendingControllerValues.load (std::memory_order_acquire)
            || deferredSampleNotifications.load (std::memory_order_acquire) != 0
            || deferredPluginStateNotification.load (std::memory_order_acquire))
        {
            handleAsyncUpdate();
        }

        // F10: a freeze captured its centre on the audio thread. Write it to scan_position here,
        // so saving and restoring the patch returns to the frozen place. Never from processBlock.
        for (size_t layer = 0; layer < engines.size(); ++layer)
        {
            double frozenCentre = 0.0;

            if (engines[layer].takeFreezeLatch (frozenCentre))
                setParameterValue (layer == 0 ? params::layer1ScanPosition : params::layer2ScanPosition,
                                   static_cast<float> (frozenCentre));
        }

        // A finished render is collected here even when no export window is open, so
        // the drag tile and the toast are right whichever window the user has.
        WavExporter::Result finished;

        if (exportController.getState() == ExportController::State::finished)
            collectFinishedExport (finished);
    }

    bool TextureAudioProcessor::startExportJob (ExportController::Request request, bool forDrag)
    {
        DragRender drag;
        drag.sampleRate = request.sampleRate;
        drag.bitDepth = request.bitDepth;

        if (! exportController.start (std::move (request)))
            return false;

        dragRender = forDrag ? std::optional<DragRender> (drag) : std::nullopt;
        return true;
    }

    bool TextureAudioProcessor::collectFinishedExport (WavExporter::Result& result)
    {
        if (! exportController.takeResult (result))
            return false;

        if (dragRender.has_value())
        {
            if (result.succeeded)
            {
                RenderedAudio audio;
                audio.file = result.file;
                audio.name = result.file.getFileNameWithoutExtension();
                audio.lengthSeconds = result.lengthSeconds;
                audio.sampleRate = dragRender->sampleRate;
                audio.bitDepth = dragRender->bitDepth;
                audio.bytes = result.fileSizeBytes;
                audio.peaks = result.peaks;
                lastRender = std::move (audio);
            }

            dragRender.reset();
        }

        if (result.clippedSamples > 0 && result.succeeded)
            result.message << " " << juce::String (result.clippedSamples)
                           << " samples reached full scale; nothing was limited.";

        lastExportResult = result;
        ++exportSerial;

        postToast (result.succeeded ? result.message
                                    : (result.cancelled ? juce::String ("Render cancelled. Nothing was written.")
                                                        : result.message),
                   result.succeeded ? SampleLoader::Severity::info : SampleLoader::Severity::warning);
        return true;
    }

    void TextureAudioProcessor::handleAsyncUpdate()
    {
        if (presetDirtyPending.exchange (false, std::memory_order_acq_rel))
            markPresetDirty();
        const auto written = factorySamplesWritten.exchange (-1, std::memory_order_acq_rel);

        if (written > 0)
        {
            logInfo ("factory", juce::String (written) + " factory sample(s) generated.");
            presetManager.refresh();
            postToast ("Factory samples ready.", SampleLoader::Severity::info);
            notifyPluginStateChanged();
        }
        else if (written == 0)
        {
            presetManager.refresh();
        }

        if (midiLearnBindingPending.exchange (false, std::memory_order_acq_rel))
        {
            if (midiLearn.applyPendingBinding())
            {
                postToast ("MIDI CC learned.", SampleLoader::Severity::info);
                notifyPluginStateChanged();
            }
        }

        if (hasPendingControllerValues.exchange (false, std::memory_order_acq_rel))
        {
            for (int cc = 0; cc < MidiLearn::numControllers; ++cc)
            {
                const auto value = pendingControllerValues[static_cast<size_t> (cc)]
                                       .exchange (-1, std::memory_order_acq_rel);

                if (value < 0)
                    continue;

                applyMidiLearnedController (cc, value);
            }
        }

        const auto sampleMask = deferredSampleNotifications.exchange (0, std::memory_order_acq_rel);
        for (int layerIndex = 0; layerIndex < numLayers; ++layerIndex)
            if ((sampleMask & (1u << static_cast<unsigned int> (layerIndex))) != 0)
                notifyListeners (&Listener::sampleChanged, layerIndex);

        if (deferredPluginStateNotification.exchange (false, std::memory_order_acq_rel))
            notifyPluginStateChanged();

        juce::String message;
        SampleLoader::Severity severity = SampleLoader::Severity::info;
        bool shouldToast = false;

        {
            const juce::ScopedLock lock (toastLock);

            if (hasPendingToast)
            {
                message = pendingToastMessage;
                severity = pendingToastSeverity;
                hasPendingToast = false;
                shouldToast = true;
            }
        }

        if (shouldToast)
            listeners.call ([&] (Listener& l) { l.toastRequested (message, severity); });
    }

    void TextureAudioProcessor::applyMidiLearnedController (int ccNumber, int value)
    {
        const auto parameterID = midiLearn.getParameterForController (ccNumber);

        if (parameterID.isEmpty())
            return;

        if (auto* parameter = apvts.getParameter (parameterID))
        {
            const auto normalised = juce::jlimit (0.0f, 1.0f, static_cast<float> (value) / 127.0f);
            parameter->setValueNotifyingHost (normalised);
            markPresetDirty();
        }
    }

    // =========================================================================
    //  Listeners and toasts
    // =========================================================================

    void TextureAudioProcessor::addListener (Listener* listener)
    {
        listeners.add (listener);
    }

    void TextureAudioProcessor::removeListener (Listener* listener)
    {
        listeners.remove (listener);
    }

    void TextureAudioProcessor::postToast (const juce::String& message, SampleLoader::Severity severity)
    {
        if (message.isEmpty())
            return;

        Logging::write (toLogLevel (severity), "ui", message);

        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        {
            listeners.call ([&] (Listener& l) { l.toastRequested (message, severity); });
            return;
        }

        {
            const juce::ScopedLock lock (toastLock);
            pendingToastMessage = message;
            pendingToastSeverity = severity;
            hasPendingToast = true;
        }

        triggerAsyncUpdate();
    }

    void TextureAudioProcessor::notifyListeners (void (Listener::*callback)(int), int argument)
    {
        listeners.call ([callback, argument] (Listener& l) { (l.*callback) (argument); });
    }

    void TextureAudioProcessor::notifySampleMissing (int layerIndex, const juce::String& expectedSha256,
                                                     const juce::String& fileName)
    {
        listeners.call ([=] (Listener& l) { l.sampleMissing (layerIndex, expectedSha256, fileName); });
    }

    void TextureAudioProcessor::notifyPluginStateChanged()
    {
        listeners.call ([] (Listener& l) { l.pluginStateChanged(); });
    }

    // =========================================================================
    //  Samples
    // =========================================================================

    // =========================================================================
    //  Variation history (F09)
    // =========================================================================

    namespace
    {
        constexpr const char* previewUndoName = "Preview variation";
    }

    /** One undo step that moves the whole sound: parameters, samples and seed. */
    class TextureAudioProcessor::SoundSnapshotAction final : public juce::UndoableAction
    {
    public:
        SoundSnapshotAction (TextureAudioProcessor& host, SoundSnapshot beforeState, SoundSnapshot afterState)
            : owner (host), before (std::move (beforeState)), after (std::move (afterState))
        {
        }

        bool perform() override { return owner.applySoundSnapshot (after); }
        bool undo() override { return owner.applySoundSnapshot (before); }

    private:
        TextureAudioProcessor& owner;
        SoundSnapshot before;
        SoundSnapshot after;
    };

    /** Adapts the processor to the pure preview state machine. */
    struct TextureAudioProcessor::VariationPreviewHost
    {
        TextureAudioProcessor& owner;

        SoundSnapshot captureState() const { return owner.captureSoundSnapshot(); }

        void applyPreview (const SoundSnapshot& target)
        {
            owner.undoManager.beginNewTransaction (previewUndoName);
            owner.undoManager.perform (new SoundSnapshotAction (owner, owner.captureSoundSnapshot(), target));
        }

        void restorePreview (const SoundSnapshot& /*before*/)
        {
            // Undo is exact while the preview is the newest step. If the user has edited
            // since, that edit wins: the preview is kept rather than silently reverted.
            if (owner.undoManager.getUndoDescription() == previewUndoName && owner.undoManager.undo())
                return;

            owner.postToast ("The preview stays: something changed after it.", SampleLoader::Severity::info);
        }
    };

    SoundSnapshot TextureAudioProcessor::captureSoundSnapshot() const
    {
        SoundSnapshot snapshot;

        for (size_t i = 0; i < snapshot.parameters.size(); ++i)
        {
            const auto* id = params::allIDs[i];
            snapshot.parameters[i] = getParameterValue (id, params::defaultValueFor (id));
        }

        for (size_t layer = 0; layer < static_cast<size_t> (numLayers); ++layer)
        {
            snapshot.media[layer] = getSampleForLayer (static_cast<int> (layer));
            snapshot.files[layer] = layers[layer].file;
            snapshot.originalNames[layer] = layers[layer].originalName;
            snapshot.sampleStarts[layer] = getSampleStart (static_cast<int> (layer));
        }

        snapshot.seed = randomSeed.load (std::memory_order_relaxed);
        snapshot.lfoCurve = lfoCurve;
        return snapshot;
    }

    bool TextureAudioProcessor::applySoundSnapshot (const SoundSnapshot& snapshot)
    {
        // The same validator as preset loading: every value finite and inside its range.
        for (size_t i = 0; i < snapshot.parameters.size(); ++i)
            if (! PresetManager::isValidParameterValue (params::allIDs[i], snapshot.parameters[i]))
                return false;

        for (const auto start : snapshot.sampleStarts)
            if (! std::isfinite (start) || start < 0.0f || start > 1.0f)
                return false;

        for (size_t i = 0; i < snapshot.parameters.size(); ++i)
            setParameterValue (params::allIDs[i], snapshot.parameters[i]);

        applyModulationCurve (snapshot.lfoCurve);

        enforceTierLimits();
        randomSeed.store (snapshot.seed, std::memory_order_relaxed);

        for (size_t layer = 0; layer < static_cast<size_t> (numLayers); ++layer)
        {
            const auto index = static_cast<int> (layer);
            auto& state = layers[layer];

            // A load still in flight must not overwrite the sound being restored.
            sampleLoader.cancelPendingLoad (index);

            const auto target = snapshot.media[layer] != nullptr ? snapshot.media[layer] : SampleBuffer::makeEmpty();

            if (target.get() != state.messageThreadCopy.get())
                publishSample (state, target);

            state.file = snapshot.files[layer];
            state.originalName = snapshot.originalNames[layer];
            setSampleStart (index, snapshot.sampleStarts[layer], false);
            notifyListeners (&Listener::sampleChanged, index);
        }

        markPresetDirty();
        notifyPluginStateChanged();
        return true;
    }

    std::size_t TextureAudioProcessor::getRetainedMediaBytes() const
    {
        std::vector<const SampleBuffer*> references;

        for (size_t i = 0; i < variations.size(); ++i)
            for (const auto& media : variations.at (i).payload.media)
                references.push_back (media.get());

        return uniqueMediaBytes (std::move (references));
    }

    void TextureAudioProcessor::recordVariation (const juce::String& operation)
    {
        // A new variation supersedes any preview: the sound now heard is the new one.
        variationPreview.accept();

        const auto candidate = captureSoundSnapshot();

        // Keep retained sample audio within budget by evicting the oldest unpinned entries.
        const auto bytesWithCandidate = [this, &candidate]
        {
            std::vector<const SampleBuffer*> references;

            for (size_t i = 0; i < variations.size(); ++i)
                for (const auto& media : variations.at (i).payload.media)
                    references.push_back (media.get());

            for (const auto& media : candidate.media)
                references.push_back (media.get());

            return uniqueMediaBytes (std::move (references));
        };

        while (bytesWithCandidate() > variationMediaBudgetBytes && variations.evictOldestUnpinned())
        {
        }

        if (bytesWithCandidate() > variationMediaBudgetBytes)
        {
            postToast ("Variations are holding too much sample audio. Unpin or clear some to keep this one.",
                       SampleLoader::Severity::warning);
            return;
        }

        const auto result = variations.add (candidate, operation.toStdString(), juce::Time::currentTimeMillis());

        if (result == VariationList::AddResult::rejectedAllPinned)
            postToast ("Every variation is pinned. Unpin one, or clear the history, to keep new ones.",
                       SampleLoader::Severity::warning);
    }

    void TextureAudioProcessor::previewVariation (std::size_t index)
    {
        const auto* entry = variations.find (index);

        if (entry == nullptr)
            return;

        VariationPreviewHost host { *this };
        variationPreview.begin (host, index, entry->payload);
    }

    bool TextureAudioProcessor::previousVariation()
    {
        if (! variations.movePrevious())
            return false;

        previewVariation (variations.getCursor());
        return true;
    }

    bool TextureAudioProcessor::nextVariation()
    {
        if (! variations.moveNext())
            return false;

        previewVariation (variations.getCursor());
        return true;
    }

    void TextureAudioProcessor::acceptVariation()
    {
        variationPreview.accept();
    }

    void TextureAudioProcessor::cancelVariation()
    {
        VariationPreviewHost host { *this };
        variationPreview.cancel (host);
    }

    bool TextureAudioProcessor::setVariationPinned (std::size_t index, bool shouldPin)
    {
        if (! variations.setPinned (index, shouldPin))
        {
            postToast ("Only eight variations can be pinned at once.", SampleLoader::Severity::warning);
            return false;
        }

        return true;
    }

    void TextureAudioProcessor::clearVariations()
    {
        // Clearing the list keeps the sound the user is hearing.
        variationPreview.accept();
        variations.clear();
    }

    void TextureAudioProcessor::setActiveLayer (int layerIndex)
    {
        activeLayer = juce::jlimit (0, numLayers - 1, layerIndex);
        notifyPluginStateChanged();
    }

    void TextureAudioProcessor::loadFileIntoLayer (const juce::File& file, int layerIndex)
    {
        const auto layer = juce::jlimit (0, numLayers - 1, layerIndex);

        if (SampleLoader::isPresetFile (file))
        {
            loadPreset (file);
            return;
        }

        if (file.isDirectory())
        {
            postToast ("Please drop an audio file, not a folder.", SampleLoader::Severity::warning);
            return;
        }

        if (! sampleLoader.isSupportedAudioFile (file))
        {
            postToast ("Unsupported format. Try WAV, AIFF, FLAC, MP3, or OGG.", SampleLoader::Severity::warning);
            return;
        }

        activeLayer = layer;
        setSampleStart (layer, 0.0f);   // a new sample starts from its beginning
        logInfo ("loader", "Loading " + file.getFullPathName() + " into layer " + juce::String (layer + 1));
        sampleLoadMarksDirty[static_cast<size_t> (layer)].store (true, std::memory_order_release);
        sampleLoader.loadAsync (file, layer);
    }

    void TextureAudioProcessor::releaseRetiredIfSafe (Layer& layer)
    {
        if (layer.retired.empty())
            return;

        if (! isPrepared)
        {
            layer.retired.clear();
            return;
        }

        const auto completedBlocks = blocksProcessed.load (std::memory_order_acquire);

        layer.retired.erase (
            std::remove_if (layer.retired.begin(), layer.retired.end(),
                            [completedBlocks] (const Layer::RetiredSample& retired)
                            {
                                return completedBlocks > retired.retiredAtBlock + 1;
                            }),
            layer.retired.end());
    }

    void TextureAudioProcessor::publishSample (Layer& layer, SampleBufferPtr next)
    {
        jassert (next != nullptr);

        releaseRetiredIfSafe (layer);

        if (layer.messageThreadCopy != nullptr)
        {
            layer.retired.push_back ({
                std::move (layer.messageThreadCopy),
                blocksProcessed.load (std::memory_order_acquire)
            });
        }

        layer.messageThreadCopy = std::move (next);

        // The swap itself: one lock-free pointer store (05_SAMPLE_LOADER.md). The
        // audio thread may still be reading the old buffer for the rest of this
        // block, which is why `retired` keeps it alive.
        layer.live.store (layer.messageThreadCopy.get(), std::memory_order_release);

        releaseRetiredIfSafe (layer);
    }

    void TextureAudioProcessor::onSampleLoaded (const SampleLoader::LoadResult& result, int layerIndex)
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));

        if (result.succeeded && result.buffer != nullptr)
        {
            auto& layer = layers[index];

            publishSample (layer, result.buffer);
            layer.file = result.file;
            layer.originalName = result.file.getFileName();

            if (sampleLoadMarksDirty[index].load (std::memory_order_acquire))
                markPresetDirty();

            if (auto* store = applicationProperties.getUserSettings())
            {
                store->setValue ("last_sample_directory", result.file.getParentDirectory().getFullPathName());
                store->saveIfNeeded();
            }

            logInfo ("loader", "Loaded " + result.file.getFileName() + " ("
                             + juce::String (result.buffer->getLengthSeconds(), 2) + " s, "
                             + juce::String (result.buffer->getOriginalChannelCount()) + " ch @ "
                             + juce::String (result.buffer->getOriginalSampleRate(), 0) + " Hz)");
        }
        else
        {
            logWarn ("loader", "Load failed: " + result.message);
        }

        if (result.message.isNotEmpty())
            postToast (result.message, result.severity);

        notifyListeners (&Listener::sampleChanged, static_cast<int> (index));
    }

    SampleBufferPtr TextureAudioProcessor::getSampleForLayer (int layerIndex) const
    {
        // The owning copy, not the raw `live` pointer: every caller is on the
        // message thread and wants something it can safely hold on to.
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        return layers[index].messageThreadCopy;
    }

    TextureAudioProcessor::BundleSource TextureAudioProcessor::captureBundleSource (const juce::String& name,
                                                                                   const juce::String& category,
                                                                                   const juce::StringArray& tags) const
    {
        BundleSource source;
        source.preset = captureCurrentPreset (name, category, tags);

        for (int layer = 0; layer < numLayers; ++layer)
            source.files[static_cast<size_t> (layer)] = getSampleFileForLayer (layer);

        return source;
    }

    void TextureAudioProcessor::relinkLayer (int layerIndex, const juce::File& file)
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));

        if (! file.existsAsFile() || ! sampleLoader.isSupportedAudioFile (file))
        {
            postToast ("That file cannot be used as a sample.", SampleLoader::Severity::warning);
            return;
        }

        logInfo ("loader", "Relinking layer " + juce::String (layerIndex + 1) + " to " + file.getFullPathName());
        sampleLoadMarksDirty[index].store (true, std::memory_order_release);
        sampleLoader.loadAsync (file, layerIndex);
        markPresetDirty();
    }

    std::shared_ptr<const PatchSnapshot> TextureAudioProcessor::captureExportSnapshot() const
    {
        auto snapshot = std::make_shared<PatchSnapshot>();
        snapshot->settings = captureRenderSettings();

        for (int layer = 0; layer < numLayers; ++layer)
            snapshot->samples[static_cast<size_t> (layer)] = getSampleForLayer (layer);

        return snapshot;
    }

    juce::File TextureAudioProcessor::getRenderedAudioDirectory()
    {
        if (auto* store = applicationProperties.getUserSettings())
        {
            const auto saved = juce::File (store->getValue ("renderedAudioDirectory"));

            if (saved != juce::File() && saved.isDirectory())
                return saved;
        }

        return RenderedAudioCache::defaultDirectory();
    }

    bool TextureAudioProcessor::setRenderedAudioDirectory (const juce::File& directory)
    {
        if (exportController.isBusy() || directory == juce::File() || ! directory.isDirectory())
            return false;

        if (auto* store = applicationProperties.getUserSettings())
        {
            store->setValue ("renderedAudioDirectory", directory.getFullPathName());
            store->saveIfNeeded();
        }

        renderedAudioCache.setDirectory (directory);
        return true;
    }

    juce::File TextureAudioProcessor::getPortableStagingDirectory() const
    {
        return PresetManager::getPluginDataDirectory().getChildFile ("Staging");
    }

    juce::File TextureAudioProcessor::getSampleFileForLayer (int layerIndex) const
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        return layers[index].file;
    }

    void TextureAudioProcessor::setSampleStart (int layerIndex, float normalised, bool shouldMarkDirty)
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        const auto clamped = juce::jlimit (0.0f, 1.0f, normalised);

        if (juce::approximatelyEqual (engines[index].getSampleStart(), clamped))
            return;

        engines[index].setSampleStart (clamped);

        if (shouldMarkDirty)
            markPresetDirty();
    }

    float TextureAudioProcessor::getSampleStart (int layerIndex) const noexcept
    {
        return engines[static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex))].getSampleStart();
    }

    void TextureAudioProcessor::clearLayer (int layerIndex)
    {
        const auto lane = juce::jlimit (0, numLayers - 1, layerIndex);
        sampleLoader.cancelPendingLoad (lane);
        setSampleStart (lane, 0.0f);

        const auto index = static_cast<size_t> (lane);
        auto& layer = layers[index];

        publishSample (layer, SampleBuffer::makeEmpty());
        layer.file = juce::File();
        layer.originalName.clear();

        engines[index].allNotesOff();

        markPresetDirty();
        notifyListeners (&Listener::sampleChanged, static_cast<int> (index));
    }

    // =========================================================================
    //  Presets
    // =========================================================================

    juce::String TextureAudioProcessor::getCurrentPresetName() const
    {
        return currentPresetName.isEmpty() ? juce::String ("Untitled") : currentPresetName;
    }

    juce::String TextureAudioProcessor::getCurrentPresetCategory() const
    {
        return currentPresetCategory;
    }

    float TextureAudioProcessor::getParameterValue (const juce::String& id, float fallback) const
    {
        if (const auto* value = apvts.getRawParameterValue (id))
            return value->load();

        return fallback;
    }

    void TextureAudioProcessor::setRandomSeed (std::uint64_t seed)
    {
        const auto previous = randomSeed.load (std::memory_order_relaxed);
        if (seed == previous)
            return;

        undoManager.beginNewTransaction ("Change random seed");
        undoManager.perform (new RandomSeedChangeAction (*this, previous, seed));
    }

    void TextureAudioProcessor::applyRandomSeedFromUndo (std::uint64_t seed)
    {
        randomSeed.store (seed, std::memory_order_relaxed);
        markPresetDirty();
        notifyPluginStateChanged();
    }

    void TextureAudioProcessor::generateNewRandomSeed()
    {
        auto& random = juce::Random::getSystemRandom();
        const auto high = static_cast<std::uint64_t> (static_cast<std::uint32_t> (random.nextInt()));
        const auto low = static_cast<std::uint64_t> (static_cast<std::uint32_t> (random.nextInt()));
        setRandomSeed ((high << 32) | low);
    }

    void TextureAudioProcessor::setParameterValue (const juce::String& id, float value)
    {
        if (auto* parameter = apvts.getParameter (id))
        {
            const auto normalised = parameter->getNormalisableRange().convertTo0to1 (value);
            parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        }
    }

    PresetManager::PresetData TextureAudioProcessor::captureCurrentPreset (const juce::String& name,
                                                                          const juce::String& category,
                                                                          const juce::StringArray& tags) const
    {
        PresetManager::PresetData data;
        data.name = name;
        data.category = category;
        data.tags = tags;
        // Never copy licensing/account identifiers into a shareable preset file.
        data.author = "User";
        data.samplePath = presetManager.makeStorablePath (getSampleFileForLayer (0));

        for (int layer = 0; layer < numLayers; ++layer)
        {
            data.samplePaths[static_cast<size_t> (layer)]
                = presetManager.makeStorablePath (getSampleFileForLayer (layer));
            data.sampleStarts[static_cast<size_t> (layer)] = getSampleStart (layer);
        }

        for (const auto* id : params::allIDs)
            data.setParameter (id, getParameterValue (id, params::defaultValueFor (id)));

        data.hasLfoCurve = true;
        data.lfoCurve = lfoCurve;

        return data;
    }

    bool TextureAudioProcessor::undoLastEdit()
    {
        undoManager.beginNewTransaction();
        return undoManager.undo();
    }

    bool TextureAudioProcessor::redoLastEdit()
    {
        undoManager.beginNewTransaction();
        return undoManager.redo();
    }

    bool TextureAudioProcessor::loadPreset (const juce::File& file)
    {
        undoManager.beginNewTransaction();
        PresetManager::PresetData data;
        juce::String error;

        const auto status = presetManager.load (file, data, error);

        if (status != PresetManager::Status::ok)
        {
            // The current state is left exactly as it was (16_ERROR_HANDLING.md).
            postToast (error.isNotEmpty() ? error : PresetManager::describe (status),
                       SampleLoader::Severity::warning);
            return false;
        }

        stopAudition();

        suppressPresetDirtyTracking.store (true, std::memory_order_release);

        for (const auto* id : params::allIDs)
            setParameterValue (id, data.getParameter (id, params::defaultValueFor (id)));

        // A preset without a curve loads the default one, the same rule as its parameters.
        // The change is part of this preset's undo step.
        const auto presetCurve = data.hasLfoCurve ? data.lfoCurve : ModulationCurve();

        if (! (presetCurve == lfoCurve))
            undoManager.perform (new ModulationCurveAction (*this, lfoCurve, presetCurve));

        currentPresetName = data.name;
        currentPresetCategory = data.category;
        currentPresetFile = file;
        presetDirty = false;

        enforceTierLimits();

        suppressPresetDirtyTracking.store (false, std::memory_order_release);
        presetDirtyPending.store (false, std::memory_order_release);

        // Preset sample state is authoritative. Clear both layers first so an
        // empty/missing reference cannot accidentally inherit audio from the prior patch.
        for (int layerIndex = 0; layerIndex < numLayers; ++layerIndex)
        {
            const auto index = static_cast<size_t> (layerIndex);
            auto& layer = layers[index];

            publishSample (layer, SampleBuffer::makeEmpty());
            layer.file = juce::File();
            layer.originalName.clear();
            engines[index].allSoundOff();
            setSampleStart (layerIndex, data.sampleStarts[index], false);

            const auto& storedPath = data.samplePaths[index];
            const auto sampleFile = presetManager.resolveSamplePath (storedPath);

            if (sampleFile.existsAsFile())
            {
                sampleLoadMarksDirty[index].store (false, std::memory_order_release);
                sampleLoader.loadAsync (sampleFile, layerIndex);
            }
            else if (storedPath.isNotEmpty())
            {
                postToast ("Preset loaded but sample '"
                               + storedPath.fromLastOccurrenceOf ("/", false, false)
                               + "' is missing in layer " + juce::String (layerIndex + 1) + ".",
                           SampleLoader::Severity::warning);
                notifySampleMissing (layerIndex, data.mediaSha256[index],
                                     storedPath.fromLastOccurrenceOf ("/", false, false));
            }

            notifyListeners (&Listener::sampleChanged, layerIndex);
        }

        logInfo ("presets", "Loaded preset " + data.name);
        notifyPluginStateChanged();

        return true;
    }

    bool TextureAudioProcessor::savePreset (const juce::File& file,
                                            const juce::String& name,
                                            const juce::String& category,
                                            const juce::StringArray& tags)
    {
        const auto data = captureCurrentPreset (name, category, tags);
        juce::String error;

        if (presetManager.save (file, data, error) != PresetManager::Status::ok)
        {
            postToast (error, SampleLoader::Severity::warning);
            return false;
        }

        currentPresetName = name;
        currentPresetCategory = category;
        currentPresetFile = file;
        presetDirty = false;

        presetManager.refresh();
        postToast ("Saved preset: " + name, SampleLoader::Severity::info);
        notifyPluginStateChanged();

        return true;
    }

    void TextureAudioProcessor::markPresetDirty()
    {
        if (presetDirty)
            return;

        presetDirty = true;
        notifyPluginStateChanged();
    }

    // =========================================================================
    //  Bulk parameter actions
    // =========================================================================

    void TextureAudioProcessor::resetToDefaults()
    {
        undoManager.beginNewTransaction();
        for (const auto* id : params::allIDs)
            setParameterValue (id, params::defaultValueFor (id));

        enforceTierLimits();
        markPresetDirty();
        postToast ("All controls reset to defaults.", SampleLoader::Severity::info);
    }

    void TextureAudioProcessor::randomiseParameters()
    {
        undoManager.beginNewTransaction();
        auto& random = juce::Random::getSystemRandom();

        // One snapshot of the locks for the whole operation (F02): a lock toggled while
        // this runs cannot change which values are written.
        const auto locks = editLocks;
        const auto write = [this, &locks] (const char* id, float value)
        {
            if (! locks.isLocked (id))
                setParameterValue (id, value);
        };

        // Start sound-design parameters from defaults so results do not drift.
        // TUNE calibrates the source sample and must survive randomisation.
        for (const auto* id : params::allIDs)
            if (juce::String (id) != params::tune
                && juce::String (id) != params::layer1Root
                && juce::String (id) != params::layer2Root
                && juce::String (id) != params::layer1FineCents
                && juce::String (id) != params::layer2FineCents
                && ! params::isToneSpaceParameter (id)
                && ! params::isPerformanceParameter (id)
                && ! params::isScanParameter (id))
                write (id, params::defaultValueFor (id));

        write (params::morph, random.nextFloat());
        write (params::attack, juce::jmap (random.nextFloat(), 5.0f, 2500.0f));
        write (params::decay, juce::jmap (random.nextFloat(), 50.0f, 3000.0f));
        write (params::sustain, juce::jmap (random.nextFloat(), 0.35f, 1.0f));
        write (params::release, juce::jmap (random.nextFloat(), 200.0f, 6000.0f));
        write (params::reverbOn, random.nextFloat() < 0.85f ? 1.0f : 0.0f);
        write (params::gainDb, juce::jmap (random.nextFloat(), -8.0f, 0.0f));

        // Tune stays put: it describes the sample, not the sound. The two grain
        // effects are applied sparingly so most rolls still sound like the sample.
        if (random.nextFloat() < 0.35f)
            write (params::reverse, juce::jmap (random.nextFloat(), 0.1f, 0.6f));

        if (random.nextFloat() < 0.35f)
            write (params::shimmer, juce::jmap (random.nextFloat(), 0.1f, 0.7f));

        if (licenseManager.isPro())
        {
            write (params::lfoRate, juce::jmap (random.nextFloat(), 0.05f, 1.5f));
            write (params::lfoDepth, random.nextFloat() < 0.5f ? 0.0f
                                                              : juce::jmap (random.nextFloat(), 0.1f, 0.7f));
            write (params::lfoSync, random.nextFloat() < 0.3f ? 1.0f : 0.0f);
            write (params::lfoDivision,
                   static_cast<float> (random.nextInt (params::numDivisions)));

            if (getSampleForLayer (1) != nullptr && getSampleForLayer (1)->isValid())
                write (params::layerBlend, juce::jmap (random.nextFloat(), 0.0f, 0.8f));
        }

        enforceTierLimits();
        markPresetDirty();
        recordVariation ("Randomize");

        const auto lockedKept = locks.countLockedTotal();
        postToast (lockedKept > 0 ? "Randomised; " + juce::String (lockedKept) + " locked "
                                        + (lockedKept == 1 ? "control" : "controls") + " kept."
                                  : juce::String ("Randomised."),
                   SampleLoader::Severity::info);
    }

    // =========================================================================
    //  Editing locks and Gentle Mutate
    // =========================================================================

    bool TextureAudioProcessor::isFieldLocked (const juce::String& parameterID) const
    {
        return editLocks.isLocked (parameterID.toStdString());
    }

    void TextureAudioProcessor::setFieldLocked (const juce::String& parameterID, bool shouldLock)
    {
        auto next = editLocks;

        if (! next.setLocked (parameterID.toStdString(), shouldLock) || next == editLocks)
            return;

        lockHistory.record (editLocks);
        editLocks = std::move (next);
        notifyPluginStateChanged();
    }

    void TextureAudioProcessor::setGroupLocked (LockGroup group, bool shouldLock)
    {
        auto next = editLocks;
        next.setGroupLocked (group, shouldLock);

        if (next == editLocks)
            return;

        lockHistory.record (editLocks);
        editLocks = std::move (next);
        notifyPluginStateChanged();
    }

    LockPolicy::GroupState TextureAudioProcessor::getGroupLockState (LockGroup group) const
    {
        return editLocks.groupState (group);
    }

    int TextureAudioProcessor::getLockedFieldCount() const
    {
        return editLocks.countLockedTotal();
    }

    void TextureAudioProcessor::unlockAllFields()
    {
        if (editLocks.empty())
            return;

        lockHistory.record (editLocks);
        editLocks.clear();
        notifyPluginStateChanged();
    }

    bool TextureAudioProcessor::undoLockChange()
    {
        LockPolicy::LockSet restored;

        if (! lockHistory.undo (restored))
            return false;

        editLocks = std::move (restored);
        notifyPluginStateChanged();
        return true;
    }

    void TextureAudioProcessor::setMutateOptions (const MutateOptions& options)
    {
        // Only the four sound groups are selectable. Pitch calibration and Layers are
        // never mutated, so their bits are dropped rather than trusted from the GUI.
        const auto selectable = MutationPlanner::groupBit (LockGroup::envelope)
                              | MutationPlanner::groupBit (LockGroup::grain)
                              | MutationPlanner::groupBit (LockGroup::motion)
                              | MutationPlanner::groupBit (LockGroup::toneSpace);

        mutateOptions.strength = std::isfinite (options.strength)
                                     ? juce::jlimit (0.0f, 1.0f, options.strength)
                                     : 0.0f;
        mutateOptions.groupMask = options.groupMask & selectable;
        mutateOptions.allowCategorical = options.allowCategorical;
    }

    std::vector<MutationField> TextureAudioProcessor::buildMutationFields() const
    {
        std::vector<MutationField> fields;
        const auto& registry = LockPolicy::fields();
        fields.reserve (registry.size());
        const auto grainSettings = getGrainControlSettings();

        for (const auto& info : registry)
        {
            MutationField field;
            field.id = info.id;
            field.group = info.group;
            field.eligible = info.mutableByDefault && (! info.proOnly || licenseManager.isPro())
                             && isGrainParameterMutable (info.id, grainSettings);

            const auto* parameter = apvts.getParameter (juce::String (std::string (info.id)));
            if (parameter == nullptr)
            {
                field.eligible = false;
                fields.push_back (field);
                continue;
            }

            field.normalised = juce::jlimit (0.0f, 1.0f, parameter->getValue());

            if (dynamic_cast<const juce::AudioParameterChoice*> (parameter) != nullptr)
            {
                field.kind = FieldKind::categorical;
                field.choiceCount = static_cast<const juce::AudioParameterChoice*> (parameter)->choices.size();
            }
            else if (dynamic_cast<const juce::AudioParameterBool*> (parameter) != nullptr)
            {
                field.kind = FieldKind::categorical;
                field.choiceCount = 2;
            }
            else if (dynamic_cast<const juce::AudioParameterInt*> (parameter) != nullptr)
            {
                field.kind = FieldKind::discrete;
                const auto range = parameter->getNormalisableRange();
                field.choiceCount = juce::roundToInt ((range.end - range.start) / range.interval) + 1;
            }
            else
            {
                field.kind = FieldKind::continuous;
            }

            fields.push_back (field);
        }

        return fields;
    }

    TextureAudioProcessor::MutateResult TextureAudioProcessor::mutateParameters()
    {
        MutateResult result;

        if (mutateOptions.strength <= 0.0f)
        {
            result.status = PlanStatus::noOp;
            result.message = "Mutate strength is zero; nothing changed.";
            return result;
        }

        // Capture everything the plan depends on before planning: the baseline values,
        // the lock snapshot, the revision and the operation seed.
        const auto revision = parameterRevision.load (std::memory_order_relaxed);
        const auto fields = buildMutationFields();
        const auto locks = editLocks;

        MutationRequest request;
        request.strength = mutateOptions.strength;
        request.seed = VersionedRandomStreams::value (randomSeed.load (std::memory_order_relaxed),
                                                      VersionedRandomStreams::Lane::mutation,
                                                      mutationOperationCount++, 0);
        request.groupMask = mutateOptions.groupMask;
        request.allowCategorical = mutateOptions.allowCategorical;
        request.baselineRevision = revision;

        const auto plan = MutationPlanner::plan (fields, locks, request, revision);
        result.status = plan.status;
        result.locked = plan.lockedSkipped;

        if (plan.status == PlanStatus::staleRevision)
            result.message = "Parameters changed while Mutate was planning. Press Mutate again.";
        else if (plan.status == PlanStatus::invalidInput)
            result.message = "Mutate skipped: a current value was not valid.";
        else if (plan.changes.empty())
            result.message = "Nothing moved" + juce::String (plan.lockedSkipped > 0
                                 ? "; " + juce::String (plan.lockedSkipped) + " locked." : ".");

        if (plan.status != PlanStatus::ok || plan.changes.empty())
        {
            if (! result.message.isEmpty())
                postToast (result.message, SampleLoader::Severity::info);
            return result;
        }

        // Writes are one undo step. A revision change here means a parameter moved
        // after the baseline was captured; nothing is written in that case.
        if (parameterRevision.load (std::memory_order_relaxed) != revision)
        {
            result.status = PlanStatus::staleRevision;
            result.message = "Parameters changed while Mutate was planning. Press Mutate again.";
            postToast (result.message, SampleLoader::Severity::warning);
            return result;
        }

        undoManager.beginNewTransaction ("Gentle Mutate");

        for (const auto& change : plan.changes)
        {
            if (auto* parameter = apvts.getParameter (juce::String (std::string (fields[change.fieldIndex].id))))
                parameter->setValueNotifyingHost (change.normalised);
        }

        enforceTierLimits();
        markPresetDirty();
        recordVariation ("Mutate");

        result.changed = static_cast<int> (plan.changes.size());
        result.message = "Changed " + juce::String (result.changed)
                       + (result.changed == 1 ? " control" : " controls")
                       + (plan.lockedSkipped > 0 ? "; " + juce::String (plan.lockedSkipped) + " locked." : ".");
        postToast (result.message, SampleLoader::Severity::info);
        return result;
    }

    void TextureAudioProcessor::hardResetAllSettings()
    {
        logWarn ("processor", "Hard reset requested: clearing preferences, mappings and license.");

        resetToDefaults();

        midiLearn.clearAll();
        licenseManager.deactivate();

        if (auto* store = applicationProperties.getUserSettings())
        {
            store->clear();
            store->saveIfNeeded();
        }

        setSecretSettings ({});

        clearLayer (0);
        clearLayer (1);

        currentPresetName.clear();
        currentPresetCategory = "Cinematic";
        currentPresetFile = juce::File();
        presetDirty = false;

        Logging::clearLog();

        presetManager.installFactoryPresetsIfNeeded();
        presetManager.refresh();

        enforceTierLimits();
        notifyPluginStateChanged();
        postToast ("All settings reset. Restart your host to finish.", SampleLoader::Severity::warning);
    }

    void TextureAudioProcessor::enforceTierLimits()
    {
        // Do not rewrite Pro parameter values in the Free tier. updatePerBlockParameters()
        // gates LFO depth and layer blend to zero in the DSP while preserving the host
        // state, so a Pro project opened or saved while unlicensed can recover its
        // settings immediately after activation.
    }

    // =========================================================================
    //  A/B compare
    // =========================================================================

    juce::ValueTree TextureAudioProcessor::captureParameterTree() const
    {
        juce::ValueTree tree ("Snapshot");

        for (const auto* id : params::allIDs)
            tree.setProperty (juce::Identifier (id), getParameterValue (id, params::defaultValueFor (id)), nullptr);

        return tree;
    }

    void TextureAudioProcessor::applyParameterTree (const juce::ValueTree& tree)
    {
        if (! tree.isValid())
            return;

        for (const auto* id : params::allIDs)
        {
            const juce::Identifier identifier (id);

            if (tree.hasProperty (identifier))
                setParameterValue (id, static_cast<float> (static_cast<double> (tree.getProperty (identifier))));
        }

        enforceTierLimits();
    }

    void TextureAudioProcessor::storeCompareSlot (int slotIndex)
    {
        const auto index = juce::jlimit (0, numCompareSlots - 1, slotIndex);
        compareSlots[static_cast<size_t> (index)] = captureParameterTree();
        compareCurves[static_cast<size_t> (index)] = lfoCurve;
    }

    void TextureAudioProcessor::recallCompareSlot (int slotIndex)
    {
        undoManager.beginNewTransaction();
        const auto index = juce::jlimit (0, numCompareSlots - 1, slotIndex);

        // Keep the slot we are leaving up to date so A/B is a true toggle.
        if (index != activeCompareSlot)
        {
            compareSlots[static_cast<size_t> (activeCompareSlot)] = captureParameterTree();
            compareCurves[static_cast<size_t> (activeCompareSlot)] = lfoCurve;
        }

        activeCompareSlot = index;
        applyParameterTree (compareSlots[static_cast<size_t> (index)]);

        // The curve is part of the slot, and undo must put back the one that was heard.
        const auto recalled = compareCurves[static_cast<size_t> (index)];

        if (! (recalled == lfoCurve))
        {
            const auto previous = lfoCurve;
            undoManager.perform (new ModulationCurveAction (*this, previous, recalled));
        }

        markPresetDirty();
        notifyPluginStateChanged();
    }

    void TextureAudioProcessor::copyCompareSlot (int fromIndex, int toIndex)
    {
        const auto from = juce::jlimit (0, numCompareSlots - 1, fromIndex);
        const auto to = juce::jlimit (0, numCompareSlots - 1, toIndex);

        if (from == activeCompareSlot)
        {
            compareSlots[static_cast<size_t> (from)] = captureParameterTree();
            compareCurves[static_cast<size_t> (from)] = lfoCurve;
        }

        compareSlots[static_cast<size_t> (to)] = compareSlots[static_cast<size_t> (from)].createCopy();
        compareCurves[static_cast<size_t> (to)] = compareCurves[static_cast<size_t> (from)];
        notifyPluginStateChanged();
    }

    // =========================================================================
    //  Export
    // =========================================================================

    WavExporter::RenderSettings TextureAudioProcessor::captureRenderSettings() const
    {
        WavExporter::RenderSettings settings;
        settings.morph = getParameterValue (params::morph, params::morphDefault);
        settings.attackMs = getParameterValue (params::attack, params::attackDefaultMs);
        settings.decayMs = getParameterValue (params::decay, params::decayDefaultMs);
        settings.sustain = getParameterValue (params::sustain, params::sustainDefault);
        settings.releaseMs = getParameterValue (params::release, params::releaseDefaultMs);
        settings.reverbOn = getParameterValue (params::reverbOn, 1.0f) > 0.5f;
        settings.gainDb = getParameterValue (params::gainDb, params::gainDefaultDb);
        settings.lfoRateHz = morphLfo.getEffectiveRateHz();
        settings.lfoDepth = licenseManager.isPro()
                                ? getParameterValue (params::lfoDepth, params::lfoDepthDefault)
                                : 0.0f;
        settings.layerBlend = licenseManager.isPro()
                                  ? getParameterValue (params::layerBlend, params::layerBlendDefault)
                                  : 0.0f;
        settings.sampleStart = { getSampleStart (0), getSampleStart (1) };
        settings.tuneSemitones = getParameterValue (params::tune, params::tuneDefaultSemitones);
        settings.sourceRootMidiNotes = {
            juce::roundToInt (getParameterValue (params::layer1Root, params::sourceRootDefaultMidi)),
            juce::roundToInt (getParameterValue (params::layer2Root, params::sourceRootDefaultMidi)) };
        settings.sourceFineCents = {
            getParameterValue (params::layer1FineCents, params::sourceFineDefaultCents),
            getParameterValue (params::layer2FineCents, params::sourceFineDefaultCents) };
        settings.reverseChance = getParameterValue (params::reverse, params::reverseDefault);
        settings.shimmerAmount = getParameterValue (params::shimmer, params::shimmerDefault);
        settings.randomMode = juce::roundToInt (getParameterValue (params::randomMode,
                                                                     static_cast<float> (params::randomModeDefault)));
        settings.randomSeed = randomSeed.load (std::memory_order_relaxed);
        settings.randomAlgorithmVersion = VersionedRandomStreams::algorithmVersion;
        settings.toneSpace = readToneSpaceSettings();
        settings.mono = readPerformanceSettings();
        settings.grain = readGrainSettings();
        settings.lfoShape = juce::roundToInt (getParameterValue (params::lfoShape, static_cast<float> (params::lfoShapeDefault)));
        settings.lfoRetrigger = juce::roundToInt (getParameterValue (params::lfoRetrigger, static_cast<float> (params::lfoRetriggerDefault)));
        settings.lfoPhase = getParameterValue (params::lfoPhase, params::lfoPhaseDefault);
        settings.lfoSlewMs = getParameterValue (params::lfoSlewMs, params::lfoSlewMsDefault);
        settings.lfoCurve = lfoCurve;
        settings.hostBpm = lastHostBpm.load (std::memory_order_relaxed);
        settings.scan = { getScanSettings (0), getScanSettings (1) };

        const auto secret = getSecretSettings();
        settings.gritBits = secret.bitDepth;
        settings.gritDownsample = secret.downsample;
        settings.gritMix = secret.mix;

        return settings;
    }

    // =========================================================================
    //  Metering
    // =========================================================================

    int TextureAudioProcessor::getActiveVoiceCount() const noexcept
    {
        int total = 0;

        for (const auto& engine : engines)
            total += engine.getActiveVoiceCount();

        return total;
    }

    float TextureAudioProcessor::getReadPosition() const noexcept
    {
        return engines[0].getDisplayReadPosition();
    }

    // =========================================================================
    //  State
    // =========================================================================

    void TextureAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        juce::ValueTree root (StateSchema::rootTag);
        root.setProperty (StateSchema::versionAttribute, pluginVersionString(), nullptr);
        const auto seedHex = VersionedRandomStreams::seedToHex (
            randomSeed.load (std::memory_order_relaxed));
        root.setProperty ("randomSeedV1", juce::String (seedHex.c_str()), nullptr);
        root.setProperty ("randomAlgorithmVersion", static_cast<int> (VersionedRandomStreams::algorithmVersion), nullptr);

        juce::ValueTree parameters (StateSchema::parametersTag);
        parameters.appendChild (apvts.copyState(), nullptr);
        root.appendChild (parameters, nullptr);

        for (int i = 0; i < numLayers; ++i)
        {
            juce::ValueTree sample (StateSchema::sampleTag);
            sample.setProperty ("index", i, nullptr);
            sample.setProperty ("path", layers[static_cast<size_t> (i)].file.getFullPathName(), nullptr);
            sample.setProperty ("originalName", layers[static_cast<size_t> (i)].originalName, nullptr);
            sample.setProperty ("start", getSampleStart (i), nullptr);
            root.appendChild (sample, nullptr);
        }

        juce::ValueTree feature (StateSchema::featureStateTag);
        feature.setProperty ("schema", StateSchema::featureStateSchemaVersion, nullptr);

        for (const auto& id : editLocks.lockedIds())
        {
            juce::ValueTree lock (StateSchema::lockTag);
            lock.setProperty ("id", juce::String (id), nullptr);
            feature.appendChild (lock, nullptr);
        }

        // F14: the curve is written only when it differs from the default, so patches that
        // do not use it stay readable by builds that predate it.
        if (! (lfoCurve == ModulationCurve()))
            feature.appendChild (StateSchema::curveToTree (lfoCurve), nullptr);

        root.appendChild (feature, nullptr);

        juce::ValueTree preset (StateSchema::presetTag);
        preset.setProperty ("name", currentPresetName, nullptr);
        preset.setProperty ("category", currentPresetCategory, nullptr);
        root.appendChild (preset, nullptr);

        // The active slot may have been edited since the last A/B switch.
        compareSlots[static_cast<size_t> (activeCompareSlot)] = captureParameterTree();
        compareCurves[static_cast<size_t> (activeCompareSlot)] = lfoCurve;

        juce::ValueTree compare (StateSchema::compareTag);
        compare.setProperty ("active", activeCompareSlot, nullptr);

        for (int slotIndex = 0; slotIndex < numCompareSlots; ++slotIndex)
        {
            juce::ValueTree slot (StateSchema::compareSlotTag);
            slot.setProperty ("index", slotIndex, nullptr);
            const auto& snapshot = compareSlots[static_cast<size_t> (slotIndex)];

            for (const auto* id : params::allIDs)
            {
                const juce::Identifier identifier (id);
                slot.setProperty (identifier,
                                  snapshot.hasProperty (identifier)
                                      ? snapshot.getProperty (identifier)
                                      : juce::var (getParameterValue (id, params::defaultValueFor (id))),
                                  nullptr);
            }

            const auto& curve = compareCurves[static_cast<size_t> (slotIndex)];

            if (! (curve == ModulationCurve()))
                slot.appendChild (StateSchema::curveToTree (curve), nullptr);

            compare.appendChild (slot, nullptr);
        }

        root.appendChild (compare, nullptr);

        const auto secret = getSecretSettings();
        juce::ValueTree secretState (StateSchema::secretTag);
        secretState.setProperty ("bits", secret.bitDepth, nullptr);
        secretState.setProperty ("downsample", secret.downsample, nullptr);
        secretState.setProperty ("mix", secret.mix, nullptr);
        secretState.setProperty ("revealed", secret.revealed, nullptr);
        root.appendChild (secretState, nullptr);

        if (auto xml = root.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void TextureAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        auto xml = getXmlFromBinary (data, sizeInBytes);

        if (xml == nullptr)
        {
            logWarn ("state", "Could not parse plugin state; leaving current state untouched.");
            return;
        }

        const auto root = juce::ValueTree::fromXml (*xml);
        const auto validation = StateSchema::validate (root);

        if (! validation.isOk())
        {
            logWarn ("state", validation.message + " Leaving current state untouched.");
            return;
        }

        stopAudition();

        const auto parameters = root.getChildWithName (StateSchema::parametersTag);
        suppressPresetDirtyTracking.store (true, std::memory_order_release);
        apvts.replaceState (parameters.getChild (0).createCopy());

        // Parameters added after the state was written are absent from it, and
        // replaceState leaves absent parameters alone. Give them their defaults here,
        // so an older session never inherits the previous patch's tone or performance
        // settings. Done before the undo history is cleared, so it is not undoable.
        StateSchema::applyDefaultsForMissingParameters (
            parameters.getChild (0),
            [this] (const char* id) { setParameterValue (id, params::defaultValueFor (id)); });
        randomSeed.store (VersionedRandomStreams::defaultSeed, std::memory_order_relaxed);
        if (root.hasProperty ("randomSeedV1"))
        {
            std::uint64_t restoredSeed = 0;
            if (VersionedRandomStreams::parseSeedHex (root.getProperty ("randomSeedV1").toString().toStdString(), restoredSeed))
                randomSeed.store (restoredSeed, std::memory_order_relaxed);
        }
        // A restored session starts a fresh history: undo must never step back into
        // whatever this instance was doing before the host loaded the project.
        undoManager.clearUndoHistory();

        // Variations are session-local: a project restore starts a fresh history.
        variationPreview.reset();
        variations.clear();

        // Locks belong to this instance's host state. Restore applies values regardless
        // of locks; it only reinstates which controls are protected from later edits.
        editLocks.clear();
        lockHistory.clear();

        if (const auto feature = root.getChildWithName (StateSchema::featureStateTag); feature.isValid())
        {
            std::vector<std::string> lockIds;

            for (const auto& lock : feature)
                if (lock.getType() == StateSchema::lockTag)
                    lockIds.push_back (lock.getProperty ("id").toString().toStdString());

            const auto parsed = LockPolicy::parseIds (lockIds);
            editLocks = parsed.locks;

            for (const auto& ignored : parsed.ignoredIds)
                logWarn ("locks", "Ignored unknown lock ID in host state: " + juce::String (ignored));
        }

        // F14: a state without the curve reads the default, never the previous patch's curve.
        ModulationCurve restoredCurve;

        if (const auto feature = root.getChildWithName (StateSchema::featureStateTag); feature.isValid())
            if (const auto curveTree = feature.getChildWithName (StateSchema::lfoCurveTag); curveTree.isValid())
                StateSchema::curveFromTree (curveTree, restoredCurve);

        applyModulationCurve (restoredCurve);
        compareCurves.fill (ModulationCurve());

        const auto preset = root.getChildWithName (StateSchema::presetTag);

        currentPresetFile = juce::File();
        currentPresetName.clear();
        currentPresetCategory = "Cinematic";

        if (preset.isValid())
        {
            currentPresetName = preset.getProperty ("name", "").toString();
            currentPresetCategory = preset.getProperty ("category", "Cinematic").toString();
        }

        if (const auto compare = root.getChildWithName (StateSchema::compareTag); compare.isValid())
        {
            activeCompareSlot = juce::jlimit (0, numCompareSlots - 1,
                                              static_cast<int> (compare.getProperty ("active", 0)));

            for (const auto& child : compare)
            {
                if (child.getType() != StateSchema::compareSlotTag)
                    continue;

                const auto slotIndex = juce::jlimit (
                    0, numCompareSlots - 1, static_cast<int> (child.getProperty ("index", 0)));

                juce::ValueTree snapshot ("Snapshot");

                for (const auto* id : params::allIDs)
                {
                    const juce::Identifier identifier (id);
                    snapshot.setProperty (identifier,
                                          child.hasProperty (identifier)
                                              ? child.getProperty (identifier)
                                              : juce::var (getParameterValue (id, params::defaultValueFor (id))),
                                          nullptr);
                }

                ModulationCurve slotCurve;
                if (const auto curveTree = child.getChildWithName (StateSchema::lfoCurveTag); curveTree.isValid())
                    StateSchema::curveFromTree (curveTree, slotCurve);
                compareCurves[static_cast<size_t> (slotIndex)] = slotCurve;

                compareSlots[static_cast<size_t> (slotIndex)] = std::move (snapshot);
            }
        }
        else
        {
            activeCompareSlot = 0;
            const auto snapshot = captureParameterTree();

            for (auto& slot : compareSlots)
                slot = snapshot.createCopy();

            // Slots from before F14 hold the neutral curve, which is what the patch had.
            compareCurves.fill (restoredCurve);
        }

        SecretSettings secret;

        if (const auto secretState = root.getChildWithName (StateSchema::secretTag); secretState.isValid())
        {
            secret.bitDepth = static_cast<float> (static_cast<double> (
                secretState.getProperty ("bits", SecretEffect::defaultBits)));
            secret.downsample = static_cast<float> (static_cast<double> (
                secretState.getProperty ("downsample", SecretEffect::defaultDownsample)));
            secret.mix = static_cast<float> (static_cast<double> (
                secretState.getProperty ("mix", SecretEffect::defaultMix)));
            secret.revealed = static_cast<bool> (secretState.getProperty ("revealed", false));
        }

        setSecretSettings (secret);

        cacheParameterPointers();
        enforceTierLimits();
        suppressPresetDirtyTracking.store (false, std::memory_order_release);
        presetDirtyPending.store (false, std::memory_order_release);

        // Host state is authoritative for media too. Empty both slots first so
        // missing or absent sample records cannot inherit audio from prior state.
        for (int layerIndex = 0; layerIndex < numLayers; ++layerIndex)
        {
            sampleLoader.cancelPendingLoad (layerIndex);
            const auto index = static_cast<size_t> (layerIndex);
            auto& layer = layers[index];
            publishSample (layer, SampleBuffer::makeEmpty());
            layer.file = juce::File();
            layer.originalName.clear();
            engines[index].allSoundOff();
            setSampleStart (layerIndex, 0.0f, false);
            deferredSampleNotifications.fetch_or (
                1u << static_cast<unsigned int> (layerIndex), std::memory_order_release);
        }

        // Sample loads are queued, never performed inline: setStateInformation must
        // return promptly (13_STATE_SAVING.md, 21_WHAT_NOT_TO_DO.md item 24).
        for (const auto& child : root)
        {
            if (child.getType() != StateSchema::sampleTag)
                continue;

            const auto index = juce::jlimit (0, numLayers - 1, static_cast<int> (child.getProperty ("index", 0)));
            const auto path = child.getProperty ("path", "").toString();
            const auto originalName = child.getProperty ("originalName", "").toString();
            setSampleStart (index, static_cast<float> (static_cast<double> (child.getProperty ("start", 0.0))), false);

            if (path.isEmpty())
                continue;

            juce::File file (path);

            if (! file.existsAsFile())
            {
                const auto fallback = presetManager.resolveSamplePath (originalName.isNotEmpty() ? originalName
                                                                                                 : path);

                if (fallback.existsAsFile())
                {
                    file = fallback;
                }
                else
                {
                    postToast ("Sample '" + (originalName.isNotEmpty() ? originalName : path)
                                   + "' not found. Drop a file to replace it.",
                               SampleLoader::Severity::warning);
                    continue;
                }
            }

            sampleLoadMarksDirty[static_cast<size_t> (index)].store (false, std::memory_order_release);
            sampleLoader.loadAsync (file, index);
        }

        presetDirty = false;
        deferredPluginStateNotification.store (true, std::memory_order_release);
    }

    // =========================================================================
    //  Diagnostics
    // =========================================================================

    void TextureAudioProcessor::setCrashLogEnabled (bool shouldBeEnabled)
    {
        crashLogEnabled = shouldBeEnabled;

        if (! shouldBeEnabled)
        {
            logInfo ("debug", "Crash logging disabled.");
            crashLogFile = juce::File();
            return;
        }

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
        crashLogFile = Logging::getLogDirectory().getChildFile ("crash_" + stamp + ".txt");

        // The troubleshooting snapshot is written at the head of the crash log so a
        // support ticket only ever needs the one file to be useful.
        crashLogFile.replaceWithText (buildTroubleshootingReport()
                                      + "\n\n===== SESSION LOG FOLLOWS =====\n");

        logInfo ("debug", "Crash logging enabled: " + crashLogFile.getFullPathName());
    }

    juce::String TextureAudioProcessor::buildTroubleshootingReport() const
    {
        juce::StringArray lines;

        lines.add ("===== TEXTURE TROUBLESHOOTING REPORT =====");
        lines.add ("Generated: " + juce::Time::getCurrentTime().toISO8601 (true));
        lines.add ("Plugin version: " + pluginVersionString());
        lines.add ("Build: " + juce::String (__DATE__) + " " + juce::String (__TIME__));
        lines.add ("");

        lines.add ("--- Host ---");
        lines.add ("Wrapper: " + juce::String (juce::AudioProcessor::getWrapperTypeDescription (wrapperType)));
        lines.add ("Host: " + juce::String (juce::PluginHostType().getHostDescription()));
        lines.add ("OS: " + juce::SystemStats::getOperatingSystemName());
        lines.add ("CPU: " + juce::SystemStats::getCpuVendor() + " " + juce::SystemStats::getCpuModel());
        lines.add ("Cores: " + juce::String (juce::SystemStats::getNumCpus()));
        lines.add ("RAM: " + juce::String (juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
        lines.add ("");

        lines.add ("--- Audio ---");
        lines.add ("Sample rate: " + juce::String (currentSampleRate, 1) + " Hz");
        lines.add ("Block size: " + juce::String (currentBlockSize));
        lines.add ("Prepared: " + juce::String (isPrepared ? "yes" : "no"));
        lines.add ("Output channels: " + juce::String (getMainBusNumOutputChannels()));
        lines.add ("Active voices: " + juce::String (getActiveVoiceCount()));
        lines.add ("CPU load (last block): " + juce::String (getMeasuredCpuLoad() * 100.0, 2) + "%");
        lines.add ("CPU load (peak): " + juce::String (getPeakCpuLoad() * 100.0, 2) + "%");
        lines.add ("Non-finite samples seen: "
                   + juce::String (nonFiniteDetected.load (std::memory_order_relaxed) ? "yes" : "no"));
        lines.add ("");

        lines.add ("--- Samples ---");

        for (int i = 0; i < numLayers; ++i)
        {
            const auto buffer = getSampleForLayer (i);
            lines.add ("Layer " + juce::String (i + 1) + ":");

            if (buffer != nullptr && buffer->isValid())
            {
                lines.add ("  name: " + buffer->getName());
                lines.add ("  path: " + layers[static_cast<size_t> (i)].file.getFullPathName());
                lines.add ("  length: " + juce::String (buffer->getLengthSeconds(), 3) + " s");
                lines.add ("  channels (original): " + juce::String (buffer->getOriginalChannelCount()));
                lines.add ("  rate (original): " + juce::String (buffer->getOriginalSampleRate(), 0) + " Hz");
                lines.add ("  truncated: " + juce::String (buffer->wasTruncatedOnLoad() ? "yes" : "no"));
            }
            else
            {
                lines.add ("  (empty)");
            }
        }

        lines.add ("");
        lines.add ("--- License ---");
        lines.add ("Tier: " + juce::String (licenseManager.isPro() ? "PRO" : "FREE"));
        lines.add ("Activation valid: " + juce::String (licenseManager.isPro() ? "yes" : "no"));
        lines.add ("");

        lines.add ("--- Preset ---");
        lines.add ("Name: " + getCurrentPresetName());
        lines.add ("Category: " + currentPresetCategory);
        lines.add ("Unsaved changes: " + juce::String (presetDirty ? "yes" : "no"));
        lines.add ("Preset folder: " + presetManager.getPresetSearchRoot().getFullPathName());
        lines.add ("Indexed presets: " + juce::String (static_cast<int> (presetManager.getAllPresets().size())));
        lines.add ("");

        lines.add ("--- Parameters ---");

        for (const auto* id : params::allIDs)
            lines.add (juce::String (id) + " = "
                       + juce::String (getParameterValue (id, params::defaultValueFor (id)), 4));

        lines.add ("");
        lines.add ("--- MIDI ---");
        lines.add ("Last note: " + juce::String (getLastMidiNote())
                   + " velocity " + juce::String (getLastMidiVelocity()));
        lines.add ("Last CC: " + juce::String (getLastMidiController())
                   + " value " + juce::String (getLastMidiControllerValue()));
        lines.add ("CC mappings:");
        lines.add (midiLearn.describeMappings());
        lines.add ("");

        lines.add ("--- Paths ---");
        lines.add ("Data: " + PresetManager::getPluginDataDirectory().getFullPathName());
        lines.add ("Log: " + Logging::getLogFile().getFullPathName());
        lines.add ("Factory samples: " + PresetManager::getFactorySampleDirectory().getFullPathName());
        lines.add ("User presets: " + PresetManager::getUserPresetDirectory().getFullPathName());
        lines.add ("");
        lines.add ("===== END OF REPORT =====");

        return lines.joinIntoString ("\n");
    }

    // =========================================================================
    //  Hidden effect
    // =========================================================================

    TextureAudioProcessor::SecretSettings TextureAudioProcessor::getSecretSettings() const
    {
        SecretSettings settings;
        settings.bitDepth = secretBitDepth.load (std::memory_order_relaxed);
        settings.downsample = secretDownsample.load (std::memory_order_relaxed);
        settings.mix = secretMix.load (std::memory_order_relaxed);
        settings.revealed = secretRevealed.load (std::memory_order_relaxed);

        return settings;
    }

    void TextureAudioProcessor::setSecretSettings (const SecretSettings& settings)
    {
        secretBitDepth.store (juce::jlimit (SecretEffect::minBits, SecretEffect::maxBits, settings.bitDepth),
                              std::memory_order_relaxed);
        secretDownsample.store (juce::jlimit (SecretEffect::minDownsample, SecretEffect::maxDownsample,
                                              settings.downsample),
                                std::memory_order_relaxed);
        secretMix.store (juce::jlimit (0.0f, 1.0f, settings.mix), std::memory_order_relaxed);
        secretRevealed.store (settings.revealed, std::memory_order_relaxed);

        markPresetDirty();
    }

    void TextureAudioProcessor::setSecretRevealed (bool shouldBeRevealed)
    {
        secretRevealed.store (shouldBeRevealed, std::memory_order_relaxed);

        if (! shouldBeRevealed)
            return;

        logInfo ("easter-egg", "GRIT revealed.");
    }

    // =========================================================================
    //  Editor
    // =========================================================================

    juce::AudioProcessorEditor* TextureAudioProcessor::createEditor()
    {
        return new TextureAudioProcessorEditor (*this);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new texture::TextureAudioProcessor();
}
