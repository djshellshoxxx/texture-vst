#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    namespace
    {
        constexpr const char* stateRootTag = "TexturePluginState";
        constexpr const char* parametersTag = "Parameters";
        constexpr const char* sampleTag = "Sample";
        constexpr const char* presetTag = "Preset";
        constexpr const char* versionAttribute = "pluginVersion";

        /** Morph is ramped over this long so a sweep never zips (06_MORPH_KNOB_BEHAVIOUR.md). */
        constexpr double morphSmoothingSeconds = 0.02;

        /** Peak meter fall time for the header LED. */
        constexpr float peakDecayPerBlock = 0.82f;

        juce::String pluginVersionString()
        {
           #if defined (JucePlugin_VersionString)
            return JucePlugin_VersionString;
           #else
            return "1.0.0";
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
          apvts (*this, nullptr, "TEXTURE", createParameterLayout()),
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

        sampleLoader.setCallback ([this] (const SampleLoader::LoadResult& result)
        {
            onSampleLoaded (result, activeLayer);
        });

        cacheParameterPointers();
        enforceTierLimits();

        for (auto& slot : compareSlots)
            slot = captureParameterTree();
    }

    TextureAudioProcessor::~TextureAudioProcessor()
    {
        // Shut the loader down before anything it might call back into goes away
        // (19_DEBUGGING.md, common bug 6).
        sampleLoader.setCallback (nullptr);
        sampleLoader.cancelPendingLoads();
        sampleLoader.waitForPendingLoads();

        cancelPendingUpdate();

        logInfo ("processor", "TEXTURE instance destroyed.");

        // If crash logging was armed, fold the session log into the crash file so a
        // support ticket only ever needs that one attachment.
        if (crashLogEnabled && crashLogFile != juce::File())
            crashLogFile.appendText (Logging::readLogText());

        Logging::removeClient();
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
        for (auto& engine : engines)
            engine.releaseResources();

        effects.releaseResources();
        stereoBus.setSize (0, 0);
        layerMix.setSize (0, 0);
        isPrepared = false;

        logInfo ("processor", "releaseResources");
    }

    void TextureAudioProcessor::reloadSamplesForNewSampleRate()
    {
        for (int i = 0; i < numLayers; ++i)
        {
            const auto file = layers[static_cast<size_t> (i)].file;

            if (file.existsAsFile())
            {
                const auto previousLayer = activeLayer;
                activeLayer = i;
                sampleLoader.loadAsync (file);
                activeLayer = previousLayer;
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

        if (auto* playHead = getPlayHead())
        {
            if (const auto position = playHead->getPosition())
            {
                morphLfo.updateFromPlayHead (position->getBpm().orFallback (120.0),
                                             position->getIsPlaying(),
                                             position->getPpqPosition().orFallback (0.0));
            }
        }

        morphLfo.advanceBlock (numSamples);
        morphNow = morphLfo.applyTo (morphNow);

        effectiveMorph.store (morphNow, std::memory_order_relaxed);

        currentTargets = deriveMorphTargets (morphNow);

        const auto attack  = attackParameter  != nullptr ? attackParameter->load()  : params::attackDefaultMs;
        const auto decay   = decayParameter   != nullptr ? decayParameter->load()   : params::decayDefaultMs;
        const auto sustain = sustainParameter != nullptr ? sustainParameter->load() : params::sustainDefault;
        const auto release = releaseParameter != nullptr ? releaseParameter->load() : params::releaseDefaultMs;

        for (auto& engine : engines)
            engine.setAdsrParameters (attack, decay, sustain, release);

        effects.setMorphTargets (currentTargets);
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

            const auto source = layers[static_cast<size_t> (i)].live.load (std::memory_order_acquire);

            if (source == nullptr || ! source->isValid())
                continue;

            layerMix.clear (0, numSamples);

            if (engines[static_cast<size_t> (i)].renderNextBlock (source, currentTargets, layerMix, 0, numSamples) == 0)
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
            buffer.copyFrom (0, 0, stereoBus, 0, 0, numSamples, 0.5f);
            buffer.addFrom (0, 0, stereoBus, 1, 0, numSamples, 0.5f);
        }

        // Metering.
        float peak = 0.0f;

        for (int ch = 0; ch < juce::jmin (2, numOutputChannels); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

        outputPeak.push (peak);

        const auto previousSmoothed = smoothedPeak.load (std::memory_order_relaxed);
        smoothedPeak.store (juce::jmax (peak, previousSmoothed * peakDecayPerBlock), std::memory_order_relaxed);

        const auto elapsedSeconds = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - blockStartTicks);
        const auto blockSeconds = static_cast<double> (numSamples) / currentSampleRate;
        const auto load = blockSeconds > 0.0 ? elapsedSeconds / blockSeconds : 0.0;

        cpuLoad.store (load, std::memory_order_relaxed);

        if (load > peakCpuLoad.load (std::memory_order_relaxed))
            peakCpuLoad.store (load, std::memory_order_relaxed);
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
                engine.setSustainPedal (message.isSustainPedalOn());
        }
        else if (message.isAllNotesOff())
        {
            for (auto& engine : engines)
                engine.allNotesOff();
        }
        else if (message.isAllSoundOff())
        {
            for (auto& engine : engines)
                engine.allSoundOff();

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
                triggerAsyncUpdate();
            }
            else if (midiLearn.getMappedSlotForController (controller) >= 0)
            {
                // Parameters are never written from the audio thread; hand the value
                // to the message thread instead (21_WHAT_NOT_TO_DO.md, item 5).
                pendingControllerValues[static_cast<size_t> (controller)]
                    .store (value, std::memory_order_release);
                hasPendingControllerValues.store (true, std::memory_order_release);
                triggerAsyncUpdate();
            }
        }
    }

    // =========================================================================
    //  Message-thread work queued from the audio thread
    // =========================================================================

    void TextureAudioProcessor::handleAsyncUpdate()
    {
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
        logInfo ("loader", "Loading " + file.getFullPathName() + " into layer " + juce::String (layer + 1));
        sampleLoader.loadAsync (file);
    }

    void TextureAudioProcessor::onSampleLoaded (const SampleLoader::LoadResult& result, int layerIndex)
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));

        if (result.succeeded && result.buffer != nullptr)
        {
            auto& layer = layers[index];

            // Publish to the audio thread, then let the previous buffer go on this
            // thread; the audio thread never runs a destructor (05_SAMPLE_LOADER.md).
            auto previous = layer.messageThreadCopy;
            layer.messageThreadCopy = result.buffer;
            layer.live.store (result.buffer, std::memory_order_release);
            layer.file = result.file;
            layer.originalName = result.file.getFileName();
            previous.reset();

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
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        return layers[index].live.load (std::memory_order_acquire);
    }

    juce::File TextureAudioProcessor::getSampleFileForLayer (int layerIndex) const
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        return layers[index].file;
    }

    void TextureAudioProcessor::clearLayer (int layerIndex)
    {
        const auto index = static_cast<size_t> (juce::jlimit (0, numLayers - 1, layerIndex));
        auto& layer = layers[index];

        auto previous = layer.messageThreadCopy;
        auto empty = SampleBuffer::makeEmpty();
        layer.messageThreadCopy = empty;
        layer.live.store (empty, std::memory_order_release);
        layer.file = juce::File();
        layer.originalName.clear();
        previous.reset();

        for (auto& engine : engines)
            engine.allNotesOff();

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
        data.author = licenseManager.getStoredEmail().isNotEmpty() ? licenseManager.getStoredEmail()
                                                                   : juce::String ("User");
        data.samplePath = presetManager.makeStorablePath (getSampleFileForLayer (0));

        for (const auto* id : params::allIDs)
            data.setParameter (id, getParameterValue (id, params::defaultValueFor (id)));

        return data;
    }

    bool TextureAudioProcessor::loadPreset (const juce::File& file)
    {
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

        for (const auto* id : params::allIDs)
            setParameterValue (id, data.getParameter (id, params::defaultValueFor (id)));

        currentPresetName = data.name;
        currentPresetCategory = data.category;
        presetDirty = false;

        enforceTierLimits();

        const auto sampleFile = presetManager.resolveSamplePath (data.samplePath);

        if (sampleFile.existsAsFile())
        {
            activeLayer = 0;
            sampleLoader.loadAsync (sampleFile);
        }
        else if (data.samplePath.isNotEmpty())
        {
            postToast ("Preset loaded but sample '"
                       + data.samplePath.fromLastOccurrenceOf ("/", false, false) + "' is missing.",
                       SampleLoader::Severity::warning);
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
        for (const auto* id : params::allIDs)
            setParameterValue (id, params::defaultValueFor (id));

        enforceTierLimits();
        markPresetDirty();
        postToast ("All controls reset to defaults.", SampleLoader::Severity::info);
    }

    void TextureAudioProcessor::randomiseParameters()
    {
        auto& random = juce::Random::getSystemRandom();

        // Every press starts from defaults so results do not drift towards extremes.
        for (const auto* id : params::allIDs)
            setParameterValue (id, params::defaultValueFor (id));

        setParameterValue (params::morph, random.nextFloat());
        setParameterValue (params::attack, juce::jmap (random.nextFloat(), 5.0f, 2500.0f));
        setParameterValue (params::decay, juce::jmap (random.nextFloat(), 50.0f, 3000.0f));
        setParameterValue (params::sustain, juce::jmap (random.nextFloat(), 0.35f, 1.0f));
        setParameterValue (params::release, juce::jmap (random.nextFloat(), 200.0f, 6000.0f));
        setParameterValue (params::reverbOn, random.nextFloat() < 0.85f ? 1.0f : 0.0f);
        setParameterValue (params::gainDb, juce::jmap (random.nextFloat(), -8.0f, 0.0f));

        if (licenseManager.isPro())
        {
            setParameterValue (params::lfoRate, juce::jmap (random.nextFloat(), 0.05f, 1.5f));
            setParameterValue (params::lfoDepth, random.nextFloat() < 0.5f ? 0.0f
                                                                          : juce::jmap (random.nextFloat(), 0.1f, 0.7f));
            setParameterValue (params::lfoSync, random.nextFloat() < 0.3f ? 1.0f : 0.0f);
            setParameterValue (params::lfoDivision,
                               static_cast<float> (random.nextInt (params::numDivisions)));

            if (getSampleForLayer (1) != nullptr && getSampleForLayer (1)->isValid())
                setParameterValue (params::layerBlend, juce::jmap (random.nextFloat(), 0.0f, 0.8f));
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

        clearLayer (0);
        clearLayer (1);

        currentPresetName.clear();
        currentPresetCategory = "Cinematic";
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
        if (licenseManager.isPro())
            return;

        // Pro parameters stay in the tree but are pinned to their inert values
        // (11_FREE_VS_PRO.md, "Gating enforcement").
        if (getParameterValue (params::lfoDepth, 0.0f) > 0.0f)
            setParameterValue (params::lfoDepth, 0.0f);

        if (getParameterValue (params::layerBlend, 0.0f) > 0.0f)
            setParameterValue (params::layerBlend, 0.0f);
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
        settings.gainDb = getParameterValue (params::gainDb, params::gainDefaultDb);

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
        juce::ValueTree root (stateRootTag);
        root.setProperty (versionAttribute, pluginVersionString(), nullptr);

        juce::ValueTree parameters (parametersTag);
        parameters.appendChild (apvts.copyState(), nullptr);
        root.appendChild (parameters, nullptr);

        for (int i = 0; i < numLayers; ++i)
        {
            juce::ValueTree sample (sampleTag);
            sample.setProperty ("index", i, nullptr);
            sample.setProperty ("path", layers[static_cast<size_t> (i)].file.getFullPathName(), nullptr);
            sample.setProperty ("originalName", layers[static_cast<size_t> (i)].originalName, nullptr);
            root.appendChild (sample, nullptr);
        }

        juce::ValueTree preset (presetTag);
        preset.setProperty ("name", currentPresetName, nullptr);
        preset.setProperty ("category", currentPresetCategory, nullptr);
        root.appendChild (preset, nullptr);

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

        if (! root.isValid() || root.getType().toString() != stateRootTag)
        {
            logWarn ("state", "Unexpected plugin state root; ignoring.");
            return;
        }

        const auto version = root.getProperty (versionAttribute, "1.0.0").toString();

        if (! version.startsWith ("1."))
            logWarn ("state", "State written by version " + version + "; reading on a best-effort basis.");

        const auto parameters = root.getChildWithName (parametersTag);

        if (parameters.isValid() && parameters.getNumChildren() > 0)
            apvts.replaceState (parameters.getChild (0).createCopy());

        const auto preset = root.getChildWithName (presetTag);

        if (preset.isValid())
        {
            currentPresetName = preset.getProperty ("name", "").toString();
            currentPresetCategory = preset.getProperty ("category", "Cinematic").toString();
        }

        cacheParameterPointers();
        enforceTierLimits();

        // Sample loads are queued, never performed inline: setStateInformation must
        // return promptly (13_STATE_SAVING.md, 21_WHAT_NOT_TO_DO.md item 24).
        for (const auto& child : root)
        {
            if (child.getType().toString() != sampleTag)
                continue;

            const auto index = juce::jlimit (0, numLayers - 1, static_cast<int> (child.getProperty ("index", 0)));
            const auto path = child.getProperty ("path", "").toString();
            const auto originalName = child.getProperty ("originalName", "").toString();

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

            activeLayer = index;
            sampleLoader.loadAsync (file);
        }

        presetDirty = false;
        notifyPluginStateChanged();
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
        lines.add ("Wrapper: " + juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
        lines.add ("Host: " + juce::PluginHostType().getHostDescription());
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
        lines.add ("Email: " + (licenseManager.getStoredEmail().isNotEmpty()
                                    ? licenseManager.getStoredEmail() : juce::String ("(none)")));
        lines.add ("Key (masked): " + licenseManager.getMaskedKey());
        lines.add ("Signature matches: " + juce::String (licenseManager.isPro() ? "yes" : "no"));
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
