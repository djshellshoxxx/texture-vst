// TEXTURE™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Utility/DspHelpers.h"
#include "Utility/StateSchema.h"

#include <cmath>
#include <algorithm>

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

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::voiceMode, 1 }, params::displayNameFor (params::voiceMode),
            juce::StringArray { "Poly", "Mono Retrigger", "Mono Legato" }, params::voiceModeDefault));

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { params::notePriority, 1 }, params::displayNameFor (params::notePriority),
            juce::StringArray { "Last", "High", "Low" }, params::notePriorityDefault));

        const auto hzFormatter = [] (float value, int)
        {
            return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                    : juce::String (juce::roundToInt (value)) + " Hz";
        };

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { params::toneFilterOn, 1 }, params::displayNameFor (params::toneFilterOn),
            params::toneFilterOnDefault));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneHpHz, 1 }, params::displayNameFor (params::toneHpHz),
            juce::NormalisableRange<float> { params::toneHpMinHz, params::toneHpMaxHz, 0.0f, 0.3f },
            params::toneHpDefaultHz,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::toneLpHz, 1 }, params::displayNameFor (params::toneLpHz),
            juce::NormalisableRange<float> { params::toneLpMinHz, params::toneLpMaxHz, 0.0f, 0.3f },
            params::toneLpDefaultHz,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (hzFormatter)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::spaceWidth, 1 }, params::displayNameFor (params::spaceWidth),
            Range { params::widthMin, params::widthMax, 0.01f }, params::widthDefault,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value * 100.0f)) + "%"; })));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { params::glideMs, 1 }, params::displayNameFor (params::glideMs),
            juce::NormalisableRange<float> { params::glideMinMs, params::glideMaxMs, 0.0f, 0.4f },
            params::glideDefaultMs,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction ([] (float value, int) { return juce::String (juce::roundToInt (value)) + " ms"; })));

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
        voiceModeParameter   = apvts.getRawParameterValue (params::voiceMode);
        notePriorityParameter = apvts.getRawParameterValue (params::notePriority);
        glideParameter       = apvts.getRawParameterValue (params::glideMs);
        toneFilterOnParameter = apvts.getRawParameterValue (params::toneFilterOn);
        toneHpParameter      = apvts.getRawParameterValue (params::toneHpHz);
        toneLpParameter      = apvts.getRawParameterValue (params::toneLpHz);
        widthParameter       = apvts.getRawParameterValue (params::spaceWidth);
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
        double hostPpq = -1.0;
        bool hostIsPlaying = false;

        if (auto* hostPlayHead = getPlayHead())
        {
            if (const auto position = hostPlayHead->getPosition())
            {
                hostBpm = position->getBpm().orFallback (0.0);
                hostPpq = position->getPpqPosition().orFallback (-1.0);
                hostIsPlaying = position->getIsPlaying();
            }
        }

        morphLfo.updateFromPlayHead (hostBpm, hostIsPlaying, hostPpq);
        morphLfo.advanceBlock (numSamples);
        morphNow = morphLfo.applyTo (morphNow);

        effectiveMorph.store (morphNow, std::memory_order_relaxed);

        currentTargets = deriveMorphTargets (morphNow);
        currentTargets.tuneSemitones = tuneParameter    != nullptr ? tuneParameter->load()    : params::tuneDefaultSemitones;
        currentTargets.reverseChance = reverseParameter != nullptr ? reverseParameter->load() : params::reverseDefault;
        currentTargets.shimmerAmount = shimmerParameter != nullptr ? shimmerParameter->load() : params::shimmerDefault;

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
            engines[i].setVoiceMode (voiceModeFrom (voiceModeParameter),
                                     priorityFrom (notePriorityParameter),
                                     glideParameter != nullptr ? glideParameter->load() : 0.0f);
        }

        effects.setMorphTargets (currentTargets);
        effects.getSecretEffect().setBitDepth (secretBitDepth.load (std::memory_order_relaxed));
        effects.getSecretEffect().setDownsample (secretDownsample.load (std::memory_order_relaxed));
        effects.getSecretEffect().setMix (secretMix.load (std::memory_order_relaxed));
        effects.setToneFilter (toneFilterOnParameter != nullptr && toneFilterOnParameter->load() > 0.5f,
                               toneHpParameter != nullptr ? toneHpParameter->load() : params::toneHpDefaultHz,
                               toneLpParameter != nullptr ? toneLpParameter->load() : params::toneLpDefaultHz);
        effects.setStereoWidth (widthParameter != nullptr ? widthParameter->load() : params::widthDefault);
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
        constexpr std::array<std::array<int, 4>, 4> offsets {{
            {{ 0, -1, -1, -1 }},
            {{ 0, 4, 7, -1 }},
            {{ 0, 3, 7, -1 }},
            {{ 0, 7, -1, -1 }}
        }};

        for (const auto offset : offsets[static_cast<size_t> (chord)])
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

            for (auto& engine : engines)
                engine.noteOn (message.getChannel(), message.getNoteNumber(), message.getFloatVelocity());
        }
        else if (message.isNoteOff())
        {
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
            for (auto& engine : engines)
                engine.allNotesOff (message.getChannel());
        }
        else if (message.isAllSoundOff())
        {
            stopAudition();
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

    void TextureAudioProcessor::notifyPluginStateChanged()
    {
        listeners.call ([] (Listener& l) { l.pluginStateChanged(); });
    }

    // =========================================================================
    //  Samples
    // =========================================================================

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

    void TextureAudioProcessor::mutateParameters (float amount)
    {
        std::map<juce::String, float> current;
        for (const auto* id : params::allIDs)
            current[id] = getParameterValue (id, params::defaultValueFor (id));

        const auto seed = static_cast<std::uint64_t> (juce::Random::getSystemRandom().nextInt64())
                        ^ static_cast<std::uint64_t> (juce::Time::getHighResolutionTicks());
        undoManager.beginNewTransaction();
        for (const auto& change : planMutation (current, locks, amount, seed))
            setParameterValue (change.id, change.value);

        enforceTierLimits();
        markPresetDirty();
        postToast ("Mutated the sound slightly.", SampleLoader::Severity::info);
    }

    void TextureAudioProcessor::randomiseParameters()
    {
        undoManager.beginNewTransaction();
        auto& random = juce::Random::getSystemRandom();
        const auto setIfUnlocked = [this] (const char* id, float value)
        {
            if (! locks.isLocked (id))
                setParameterValue (id, value);
        };

        // Start sound-design parameters from defaults so results do not drift.
        // TUNE calibrates the source sample and must survive randomisation.
        for (const auto* id : params::allIDs)
            if (! locks.isLocked (id) && juce::String (id) != params::tune
                && juce::String (id) != params::layer1Root
                && juce::String (id) != params::layer2Root
                && juce::String (id) != params::layer1FineCents
                && juce::String (id) != params::layer2FineCents
                && juce::String (id) != params::voiceMode
                && juce::String (id) != params::notePriority
                && juce::String (id) != params::glideMs
                && juce::String (id) != params::toneFilterOn
                && juce::String (id) != params::toneHpHz
                && juce::String (id) != params::toneLpHz
                && juce::String (id) != params::spaceWidth)
                setParameterValue (id, params::defaultValueFor (id));

        setIfUnlocked (params::morph, random.nextFloat());
        setIfUnlocked (params::attack, juce::jmap (random.nextFloat(), 5.0f, 2500.0f));
        setIfUnlocked (params::decay, juce::jmap (random.nextFloat(), 50.0f, 3000.0f));
        setIfUnlocked (params::sustain, juce::jmap (random.nextFloat(), 0.35f, 1.0f));
        setIfUnlocked (params::release, juce::jmap (random.nextFloat(), 200.0f, 6000.0f));
        setIfUnlocked (params::reverbOn, random.nextFloat() < 0.85f ? 1.0f : 0.0f);
        setIfUnlocked (params::gainDb, juce::jmap (random.nextFloat(), -8.0f, 0.0f));

        // Tune stays put: it describes the sample, not the sound. The two grain
        // effects are applied sparingly so most rolls still sound like the sample.
        if (random.nextFloat() < 0.35f)
            setIfUnlocked (params::reverse, juce::jmap (random.nextFloat(), 0.1f, 0.6f));

        if (random.nextFloat() < 0.35f)
            setIfUnlocked (params::shimmer, juce::jmap (random.nextFloat(), 0.1f, 0.7f));

        if (licenseManager.isPro())
        {
            setIfUnlocked (params::lfoRate, juce::jmap (random.nextFloat(), 0.05f, 1.5f));
            setIfUnlocked (params::lfoDepth, random.nextFloat() < 0.5f ? 0.0f
                                                                          : juce::jmap (random.nextFloat(), 0.1f, 0.7f));
            setIfUnlocked (params::lfoSync, random.nextFloat() < 0.3f ? 1.0f : 0.0f);
            setIfUnlocked (params::lfoDivision,
                               static_cast<float> (random.nextInt (params::numDivisions)));

            if (getSampleForLayer (1) != nullptr && getSampleForLayer (1)->isValid())
                setIfUnlocked (params::layerBlend, juce::jmap (random.nextFloat(), 0.0f, 0.8f));
        }

        enforceTierLimits();
        markPresetDirty();
        postToast ("Randomised.", SampleLoader::Severity::info);
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
    }

    void TextureAudioProcessor::recallCompareSlot (int slotIndex)
    {
        undoManager.beginNewTransaction();
        const auto index = juce::jlimit (0, numCompareSlots - 1, slotIndex);

        // Keep the slot we are leaving up to date so A/B is a true toggle.
        if (index != activeCompareSlot)
            compareSlots[static_cast<size_t> (activeCompareSlot)] = captureParameterTree();

        activeCompareSlot = index;
        applyParameterTree (compareSlots[static_cast<size_t> (index)]);

        markPresetDirty();
        notifyPluginStateChanged();
    }

    void TextureAudioProcessor::copyCompareSlot (int fromIndex, int toIndex)
    {
        const auto from = juce::jlimit (0, numCompareSlots - 1, fromIndex);
        const auto to = juce::jlimit (0, numCompareSlots - 1, toIndex);

        if (from == activeCompareSlot)
            compareSlots[static_cast<size_t> (from)] = captureParameterTree();

        compareSlots[static_cast<size_t> (to)] = compareSlots[static_cast<size_t> (from)].createCopy();
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
        settings.toneFilterOn = getParameterValue (params::toneFilterOn, 0.0f) > 0.5f;
        settings.toneHpHz = getParameterValue (params::toneHpHz, params::toneHpDefaultHz);
        settings.toneLpHz = getParameterValue (params::toneLpHz, params::toneLpDefaultHz);
        settings.stereoWidth = getParameterValue (params::spaceWidth, params::widthDefault);
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

        juce::ValueTree preset (StateSchema::presetTag);
        preset.setProperty ("name", currentPresetName, nullptr);
        preset.setProperty ("category", currentPresetCategory, nullptr);
        root.appendChild (preset, nullptr);

        // The active slot may have been edited since the last A/B switch.
        compareSlots[static_cast<size_t> (activeCompareSlot)] = captureParameterTree();

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

                compareSlots[static_cast<size_t> (slotIndex)] = std::move (snapshot);
            }
        }
        else
        {
            activeCompareSlot = 0;
            const auto snapshot = captureParameterTree();

            for (auto& slot : compareSlots)
                slot = snapshot.createCopy();
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
