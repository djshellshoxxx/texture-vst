#pragma once

#include "Effects/EffectsChain.h"
#include "Engine/GranularEngine.h"
#include "IO/FactoryContent.h"
#include "IO/SampleLoader.h"
#include "IO/WavExporter.h"
#include "License/LicenseManager.h"
#include "Modulation/MorphLFO.h"
#include "Presets/PresetManager.h"
#include "Utility/AtomicHelpers.h"
#include "Utility/Logging.h"
#include "Utility/MidiLearn.h"
#include "Utility/ParameterIDs.h"

#include <array>
#include <atomic>
#include <memory>

namespace texture
{
    /** The plugin.

        Deliberately thin: it owns the services, wires parameters to DSP objects and
        marshals between threads. All the interesting behaviour lives in the engine,
        the effects chain and the GUI (20_BEST_PRACTICES.md, "Testability").
    */
    class TextureAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AsyncUpdater
    {
    public:
        /** Layer 2 exists in every build but is inaudible outside Pro. */
        static constexpr int numLayers = 2;

        /** A/B compare holds two complete parameter snapshots. */
        static constexpr int numCompareSlots = 2;

        /** Anything the editor needs to know about asynchronously. */
        class Listener
        {
        public:
            virtual ~Listener() = default;

            /** A sample finished loading (or failed) for the given layer. */
            virtual void sampleChanged (int layerIndex) { juce::ignoreUnused (layerIndex); }

            /** Transient status message for the toast bar. */
            virtual void toastRequested (const juce::String& message, SampleLoader::Severity severity)
            {
                juce::ignoreUnused (message, severity);
            }

            /** The current preset name or the Pro/Free state changed. */
            virtual void pluginStateChanged() {}
        };

        TextureAudioProcessor();
        ~TextureAudioProcessor() override;

        // --- AudioProcessor ---------------------------------------------------
        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

        /** The double-precision overload is not implemented; this keeps the base
            class's version visible rather than hiding it (`-Woverloaded-virtual`). */
        using juce::AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override;
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override;

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return "Default"; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        // --- Services ---------------------------------------------------------
        juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
        const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return apvts; }

        LicenseManager& getLicenseManager() noexcept { return licenseManager; }
        PresetManager& getPresetManager() noexcept { return presetManager; }
        MidiLearn& getMidiLearn() noexcept { return midiLearn; }
        SampleLoader& getSampleLoader() noexcept { return sampleLoader; }
        juce::ApplicationProperties& getApplicationProperties() noexcept { return applicationProperties; }

        bool isPro() const noexcept { return licenseManager.isPro(); }

        /** Our own GUI listeners. The `using` declarations keep the host-facing
            `juce::AudioProcessorListener` overloads visible: without them these two
            hide the base class versions, which the wrappers call
            (`-Woverloaded-virtual`). */
        using juce::AudioProcessor::addListener;
        using juce::AudioProcessor::removeListener;

        void addListener (Listener* listener);
        void removeListener (Listener* listener);

        /** Shows a toast on the GUI and writes the same text to the log. */
        void postToast (const juce::String& message, SampleLoader::Severity severity);

        // --- Samples ----------------------------------------------------------
        /** Queues an async load into the given layer. Routes `.texture` files to the
            preset loader instead (12_PRESET_SYSTEM.md, free tier).
        */
        void loadFileIntoLayer (const juce::File& file, int layerIndex);

        /** Message-thread-safe handle on the buffer a layer is currently playing. */
        SampleBufferPtr getSampleForLayer (int layerIndex) const;

        juce::File getSampleFileForLayer (int layerIndex) const;

        void clearLayer (int layerIndex);

        /** The layer the GUI is currently editing (0 or 1). */
        int getActiveLayer() const noexcept { return activeLayer; }
        void setActiveLayer (int layerIndex);

        // --- Presets ----------------------------------------------------------
        juce::String getCurrentPresetName() const;
        juce::String getCurrentPresetCategory() const;

        bool loadPreset (const juce::File& file);
        bool savePreset (const juce::File& file,
                         const juce::String& name,
                         const juce::String& category,
                         const juce::StringArray& tags);

        /** Captures the current parameters into a PresetData ready to save. */
        PresetManager::PresetData captureCurrentPreset (const juce::String& name,
                                                        const juce::String& category,
                                                        const juce::StringArray& tags) const;

        void markPresetDirty();
        bool isPresetDirty() const noexcept { return presetDirty; }

        // --- Bulk parameter actions ------------------------------------------
        /** Restores every parameter to its documented default. */
        void resetToDefaults();

        /** Randomises every parameter into a musically sensible range. */
        void randomiseParameters();

        /** Deletes preferences, mappings, license and cached presets. Destructive. */
        void hardResetAllSettings();

        // --- A/B compare ------------------------------------------------------
        void storeCompareSlot (int slotIndex);
        void recallCompareSlot (int slotIndex);
        void copyCompareSlot (int fromIndex, int toIndex);
        int getActiveCompareSlot() const noexcept { return activeCompareSlot; }

        // --- Export -----------------------------------------------------------
        WavExporter::RenderSettings captureRenderSettings() const;

        // --- Metering and diagnostics ----------------------------------------
        float readOutputPeak() noexcept { return outputPeak.readAndClear(); }
        float getSmoothedOutputPeak() const noexcept { return smoothedPeak.load (std::memory_order_relaxed); }
        int getActiveVoiceCount() const noexcept;
        float getReadPosition() const noexcept;
        float getEffectiveMorph() const noexcept { return effectiveMorph.load (std::memory_order_relaxed); }
        double getMeasuredCpuLoad() const noexcept { return cpuLoad.load (std::memory_order_relaxed); }
        double getPeakCpuLoad() const noexcept { return peakCpuLoad.load (std::memory_order_relaxed); }
        void resetPeakCpuLoad() noexcept { peakCpuLoad.store (0.0, std::memory_order_relaxed); }

        int getLastMidiNote() const noexcept { return lastMidiNote.load (std::memory_order_relaxed); }
        int getLastMidiVelocity() const noexcept { return lastMidiVelocity.load (std::memory_order_relaxed); }
        int getLastMidiController() const noexcept { return lastMidiController.load (std::memory_order_relaxed); }
        int getLastMidiControllerValue() const noexcept { return lastMidiControllerValue.load (std::memory_order_relaxed); }

        double getCurrentSampleRate() const noexcept { return currentSampleRate; }
        int getCurrentBlockSize() const noexcept { return currentBlockSize; }

        /** Full self-diagnostic, written by the "export troubleshooting file" button. */
        juce::String buildTroubleshootingReport() const;

        /** Enables the crash-log feature for this session. Off on every load. */
        void setCrashLogEnabled (bool shouldBeEnabled);
        bool isCrashLogEnabled() const noexcept { return crashLogEnabled; }
        juce::File getCrashLogFile() const { return crashLogFile; }

        // --- Hidden effect ----------------------------------------------------
        /** GRIT lives outside the APVTS on purpose: it must not appear in the host's
            automation list or in the documented preset schema. It is persisted with
            the plugin state so a found easter egg survives a session reload.
        */
        struct SecretSettings
        {
            float bitDepth { SecretEffect::defaultBits };
            float downsample { SecretEffect::defaultDownsample };
            float mix { SecretEffect::defaultMix };
            bool revealed { false };
        };

        SecretSettings getSecretSettings() const;
        void setSecretSettings (const SecretSettings& settings);
        void setSecretRevealed (bool shouldBeRevealed);

        /** The parameter layout, shared with the tests. */
        static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

        /** Applies free-tier clamps. Called whenever the license state changes. */
        void enforceTierLimits();

    private:
        struct Layer
        {
            std::atomic<SampleBufferPtr> live { SampleBuffer::makeEmpty() };
            SampleBufferPtr messageThreadCopy;      // keeps the refcount off the audio thread
            juce::File file;
            juce::String originalName;
        };

        void installFactoryContentAsync();
        void handleAsyncUpdate() override;
        void handleMidiEvent (const juce::MidiMessage& message);
        void applyMidiLearnedController (int ccNumber, int value);
        void updatePerBlockParameters (int numSamples);
        void renderLayers (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
        void cacheParameterPointers();
        void onSampleLoaded (const SampleLoader::LoadResult& result, int layerIndex);
        void reloadSamplesForNewSampleRate();
        void notifyListeners (void (Listener::*callback)(int), int argument);
        void notifyPluginStateChanged();
        float getParameterValue (const juce::String& id, float fallback) const;
        void setParameterValue (const juce::String& id, float value);
        juce::ValueTree captureParameterTree() const;
        void applyParameterTree (const juce::ValueTree& tree);

        /** Configures the properties file before any service reads from it.

            Members initialise in declaration order, so putting this between the
            properties object and the services is what guarantees the services find
            a configured store in their own constructors.
        */
        struct StorageInitialiser
        {
            explicit StorageInitialiser (juce::ApplicationProperties& properties);
        };

        // --- Services ---------------------------------------------------------
        juce::ApplicationProperties applicationProperties;
        StorageInitialiser storageInitialiser;
        juce::AudioProcessorValueTreeState apvts;
        LicenseManager licenseManager;
        PresetManager presetManager;
        MidiLearn midiLearn;
        SampleLoader sampleLoader;

        /** One background worker for first-launch content generation. Shut down in
            the destructor before anything it calls back into goes away.
        */
        juce::ThreadPool backgroundPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1)
                                                                    .withThreadName ("Texture Background") };

        // --- DSP --------------------------------------------------------------
        std::array<GranularEngine, numLayers> engines {};
        std::array<Layer, numLayers> layers {};
        EffectsChain effects;
        MorphLFO morphLfo;

        /** Everything is rendered in stereo and folded to the host layout at the end,
            so a mono bus degrades gracefully instead of crashing (21_WHAT_NOT_TO_DO.md, 31).
        */
        juce::AudioBuffer<float> stereoBus;
        juce::AudioBuffer<float> layerMix;
        juce::SmoothedValue<float> smoothedMorph;

        MorphTargets currentTargets { deriveMorphTargets (params::morphDefault) };
        std::array<float, numLayers> layerGains { { 1.0f, 0.0f } };

        // --- Cached parameter pointers (never looked up in processBlock) ------
        std::atomic<float>* morphParameter { nullptr };
        std::atomic<float>* attackParameter { nullptr };
        std::atomic<float>* decayParameter { nullptr };
        std::atomic<float>* sustainParameter { nullptr };
        std::atomic<float>* releaseParameter { nullptr };
        std::atomic<float>* reverbOnParameter { nullptr };
        std::atomic<float>* gainParameter { nullptr };
        std::atomic<float>* lfoRateParameter { nullptr };
        std::atomic<float>* lfoDepthParameter { nullptr };
        std::atomic<float>* lfoSyncParameter { nullptr };
        std::atomic<float>* lfoDivisionParameter { nullptr };
        std::atomic<float>* layerBlendParameter { nullptr };

        // --- Host state -------------------------------------------------------
        double currentSampleRate { 44100.0 };
        int currentBlockSize { 512 };
        bool isPrepared { false };
        int activeLayer { 0 };

        // --- Preset bookkeeping ----------------------------------------------
        juce::String currentPresetName;
        juce::String currentPresetCategory { "Cinematic" };
        bool presetDirty { false };

        // --- A/B --------------------------------------------------------------
        std::array<juce::ValueTree, numCompareSlots> compareSlots;
        int activeCompareSlot { 0 };

        // --- Metering ---------------------------------------------------------
        AtomicPeak outputPeak;
        std::atomic<float> smoothedPeak { 0.0f };
        std::atomic<float> effectiveMorph { params::morphDefault };
        std::atomic<double> cpuLoad { 0.0 };
        std::atomic<double> peakCpuLoad { 0.0 };
        std::atomic<int> lastMidiNote { -1 };
        std::atomic<int> lastMidiVelocity { 0 };
        std::atomic<int> lastMidiController { -1 };
        std::atomic<int> lastMidiControllerValue { 0 };
        std::atomic<float> secretBitDepth { SecretEffect::defaultBits };
        std::atomic<float> secretDownsample { SecretEffect::defaultDownsample };
        std::atomic<float> secretMix { SecretEffect::defaultMix };
        std::atomic<bool> secretRevealed { false };

        std::atomic<int> factorySamplesWritten { -1 };
        std::atomic<bool> factoryInstallFinished { false };

        std::atomic<bool> midiLearnBindingPending { false };
        std::atomic<bool> nonFiniteDetected { false };

        /** CC values captured on the audio thread and applied on the message thread,
            so no parameter is ever written from processBlock.
        */
        std::array<std::atomic<int>, MidiLearn::numControllers> pendingControllerValues {};
        std::atomic<bool> hasPendingControllerValues { false };

        // --- Diagnostics ------------------------------------------------------
        bool crashLogEnabled { false };
        juce::File crashLogFile;

        // --- Listeners --------------------------------------------------------
        juce::ListenerList<Listener> listeners;

        juce::CriticalSection toastLock;
        juce::String pendingToastMessage;
        SampleLoader::Severity pendingToastSeverity { SampleLoader::Severity::info };
        bool hasPendingToast { false };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TextureAudioProcessor)
    };
}
