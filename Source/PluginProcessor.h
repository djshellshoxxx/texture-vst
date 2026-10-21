// TEXTURE™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary

#pragma once

#include "Effects/EffectsChain.h"
#include "Effects/ToneSpace.h"
#include "Engine/GrainTargetResolver.h"
#include "Engine/MonoNoteStack.h"
#include "Engine/SamplePositionController.h"
#include "Modulation/CurveMailbox.h"
#include "Modulation/HeldNoteCounter.h"
#include "Modulation/ModulationCurve.h"
#include "Engine/GranularEngine.h"
#include "IO/FactoryContent.h"
#include "IO/ExportController.h"
#include "IO/MediaStore.h"
#include "IO/RenderedAudioCache.h"
#include "IO/PortableMediaWorker.h"
#include "IO/SampleLoader.h"
#include "IO/WavExporter.h"
#include "License/LicenseManager.h"
#include "Modulation/MorphLFO.h"
#include "Presets/LockPolicy.h"
#include "Presets/MutationPlanner.h"
#include "Presets/PresetManager.h"
#include "Presets/SoundSnapshot.h"
#include "Presets/VariationHistory.h"
#include "Presets/VariationPreview.h"
#include "Utility/AtomicHelpers.h"
#include "Utility/Logging.h"
#include "Utility/MidiLearn.h"
#include "Utility/ParameterIDs.h"
#include "Utility/VersionedRandomStreams.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace texture
{
    /** The plugin.

        Deliberately thin: it owns the services, wires parameters to DSP objects and
        marshals between threads. All the interesting behaviour lives in the engine,
        the effects chain and the GUI (20_BEST_PRACTICES.md, "Testability").
    */
    class RandomSeedChangeAction;

    class TextureAudioProcessor final : public juce::AudioProcessor,
                                        private juce::AsyncUpdater,
                                        private juce::Timer,
                                        private juce::AudioProcessorValueTreeState::Listener
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

            /** A preset's sample could not be found. The hash is empty when the preset
                predates sound bundles (F05). */
            virtual void sampleMissing (int layerIndex, const juce::String& expectedSha256,
                                        const juce::String& fileName)
            {
                juce::ignoreUnused (layerIndex, expectedSha256, fileName);
            }
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

        /** Patch-level seed for Repeat/Evolve random lanes. The seed is state metadata,
            not a normalized host parameter. Message-thread writes only. */
        std::uint64_t getRandomSeed() const noexcept { return randomSeed.load (std::memory_order_relaxed); }
        void setRandomSeed (std::uint64_t seed);
        void generateNewRandomSeed();
        void resetRandomEvolution() noexcept { randomEvolutionResetRequested.store (true, std::memory_order_release); }

        /** Parameter undo/redo (Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y). Every mouse gesture in
            the editor, preset load, reset, randomise and A/B recall starts a new step.
            Sample, GRIT and MIDI-mapping changes are not parameters and are not
            covered. Message thread only. */
        juce::UndoManager& getUndoManager() noexcept { return undoManager; }
        bool undoLastEdit();
        bool redoLastEdit();

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

        /** Starts or replaces the internally owned audition note set. The request is
            published atomically and applied at the next audio-block boundary. */
        void setAuditionNoteSet (int rootNote, int velocity, int chord, bool enabled) noexcept;
        void stopAudition() noexcept { setAuditionNoteSet (60, 96, 0, false); }
        bool isAuditionRequested() const noexcept;

        /** Where notes start reading in a layer's sample, 0..1 (waveform click).
            Kept out of the APVTS like GRIT, persisted with the session. */
        void setSampleStart (int layerIndex, float normalised, bool markDirty = true);
        float getSampleStart (int layerIndex) const noexcept;

        /** The layer the GUI is currently editing (0 or 1). */
        int getActiveLayer() const noexcept { return activeLayer; }
        void setActiveLayer (int layerIndex);

        // --- Presets ----------------------------------------------------------
        juce::String getCurrentPresetName() const;
        juce::String getCurrentPresetCategory() const;
        juce::File getCurrentPresetFile() const { return currentPresetFile; }

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

        /** Everything a sound bundle needs, captured on the message thread. */
        struct BundleSource
        {
            PresetManager::PresetData preset;
            std::array<juce::File, numLayers> files;
        };

        BundleSource captureBundleSource (const juce::String& name,
                                          const juce::String& category,
                                          const juce::StringArray& tags) const;

        /** Points a layer at a file the user chose to replace a missing sample (F05).
            Loads asynchronously; the preset is marked dirty because its path changes. */
        void relinkLayer (int layerIndex, const juce::File& file);

        /** The patch as the offline renderer will hear it, copied on the message thread (F06). */
        std::shared_ptr<const PatchSnapshot> captureExportSnapshot() const;

        ExportController& getExportController() noexcept { return exportController; }

        /** Starts a render. `forDrag` makes the finished file the drag tile's render. The
            processor owns that bookkeeping, so it completes even when no window is open. */
        bool startExportJob (ExportController::Request request, bool forDrag);

        /** Collects a finished render and updates the drag tile. Message thread only. */
        bool collectFinishedExport (WavExporter::Result& result);

        /** The last collected render result, and a counter that changes with each one. */
        const WavExporter::Result& getLastExportResult() const noexcept { return lastExportResult; }
        int getExportSerial() const noexcept { return exportSerial; }

        /** A finished render kept for the drag tile (F15). The file is complete and named. */
        struct RenderedAudio
        {
            juce::File file;
            juce::String name;
            double lengthSeconds { 0.0 };
            double sampleRate { 48000.0 };
            int bitDepth { 24 };
            juce::int64 bytes { 0 };
            std::vector<float> peaks;
        };

        const RenderedAudio* getLastRenderedAudio() const noexcept { return lastRender.has_value() ? &*lastRender : nullptr; }

        RenderedAudioCache& getRenderedAudioCache() noexcept { return renderedAudioCache; }

        /** The user's persistent render folder, or the default when none is chosen. */
        juce::File getRenderedAudioDirectory();

        /** Saves the choice in user settings. Refused while a render is running, because a
            job may be writing into the current folder. */
        bool setRenderedAudioDirectory (const juce::File& directory);
        PortableMediaWorker& getPortableMediaWorker() noexcept { return portableMediaWorker; }
        MediaStore& getMediaStore() noexcept { return mediaStore; }
        juce::File getPortableStagingDirectory() const;

        // --- Bulk parameter actions ------------------------------------------
        /** Restores every parameter to its documented default. */
        void resetToDefaults();

        /** Randomises every parameter into a musically sensible range. Locked
            parameters are left exactly as they are (F02). */
        void randomiseParameters();

        // --- Editing locks and Gentle Mutate (F02, F03) -----------------------
        // Locks are per-instance host state. They are read only by Randomize and
        // Gentle Mutate; reset, automation, MIDI, project restore and undo ignore
        // them. Message thread only: the audio thread never reads them.

        bool isFieldLocked (const juce::String& parameterID) const;
        void setFieldLocked (const juce::String& parameterID, bool shouldLock);
        void setGroupLocked (LockGroup group, bool shouldLock);
        LockPolicy::GroupState getGroupLockState (LockGroup group) const;
        int getLockedFieldCount() const;
        void unlockAllFields();
        bool canUndoLockChange() const { return lockHistory.canUndo(); }
        bool undoLockChange();

        /** Gentle Mutate settings. Session-only: they are a tool setting, not sound. */
        struct MutateOptions
        {
            float strength { 0.15f };
            std::uint32_t groupMask { MutationPlanner::groupBit (LockGroup::grain)
                                    | MutationPlanner::groupBit (LockGroup::motion) };
            bool allowCategorical { false };
        };

        MutateOptions getMutateOptions() const { return mutateOptions; }
        void setMutateOptions (const MutateOptions& options);

        struct MutateResult
        {
            PlanStatus status { PlanStatus::noOp };
            int changed { 0 };
            int locked { 0 };
            juce::String message;
        };

        /** One Mutate press: one validated change set, one undo transaction. */
        MutateResult mutateParameters();

        /** The MORPH-derived effect values in force on the last block: 0 drive, 1 chorus
            depth, 2 reverb send, 3 shelf tilt in dB. For "Start from current sound". */
        float getFollowMorphValueForGui (int index) const noexcept;
        /** Switches to Independent and copies the current MORPH-derived values into the
            independent controls, so the sound does not jump. One undo step. Message thread. */
        void startIndependentFromCurrentSound();

        // --- Grain controls (F13) ---------------------------------------------
        /** The grain values and Follow flags as the parameters hold them. Message thread. */
        GrainControlSettings getGrainControlSettings() const;

        /** The effective value of a grain field in the last rendered block. Before the first
            block, the value is computed from the current MORPH setting. Message thread. */
        float getGrainEffectiveValue (GrainField field) const;

        /** True when the last block asked for more grains than a voice can hold. */
        bool isGrainOversubscribed() const noexcept { return grainOversubscribedFlag.load (std::memory_order_relaxed); }

        // --- Freeze and scan (F10) --------------------------------------------
        /** The scan and freeze settings a layer renders with, read from the parameters. */
        ScanSettings getScanSettings (int layerIndex) const;

        // --- Modulation shapes (F14) -----------------------------------------
        /** The user curve for the MORPH LFO's Curve shape. Message thread. */
        ModulationCurve getModulationCurve() const { return lfoCurve; }

        /** Replaces the curve as one undo step. A curve the rules refuse changes nothing and
            returns false. A dragged edit should call this once, on release. */
        bool setModulationCurve (const ModulationCurve& curve, const juce::String& undoName);

        /** Turns Follow on or off for one grain field as one undo step. Turning it off copies
            the effective value into the manual control first, so the sound does not jump. */
        void setGrainFollow (GrainField field, bool shouldFollow);

        /** Deletes preferences, mappings, license and cached presets. Destructive. */
        void hardResetAllSettings();

        // --- Variation history (F09) ------------------------------------------
        // Session-local: not saved with the host state or presets, and cleared by a
        // project restore. Message thread only. Previews are ordinary undo steps, so
        // undo and redo always see the sound the user heard.

        using VariationList = VariationHistory<SoundSnapshot>;

        /** Cap on sample audio the history may keep alive, counting each buffer once. */
        static constexpr std::size_t variationMediaBudgetBytes = 256u * 1024u * 1024u;

        const VariationList& getVariations() const noexcept { return variations; }
        std::uint64_t getVariationRevision() const noexcept { return variations.revision(); }
        std::size_t getRetainedMediaBytes() const;
        bool isPreviewingVariation() const noexcept { return variationPreview.isActive(); }
        std::size_t getPreviewingVariationIndex() const noexcept { return variationPreview.getActiveIndex(); }

        /** Records the sound as it is now, after a successful Randomize or Mutate. */
        void recordVariation (const juce::String& operation);

        /** Previous and Next walk the history and preview the entry they land on. */
        bool previousVariation();
        bool nextVariation();
        void previewVariation (std::size_t index);
        void acceptVariation();
        void cancelVariation();
        bool setVariationPinned (std::size_t index, bool shouldPin);
        void clearVariations();

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

        /** Per-channel block peak since the last read, for the stereo meter. Message thread. */
        float readChannelPeak (int channel) noexcept { return channelPeaks[static_cast<size_t> (channel & 1)].readAndClear(); }
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
        friend class RandomSeedChangeAction;
        void applyRandomSeedFromUndo (std::uint64_t seed);

        /** One sample slot.

            `live` is a raw pointer on purpose. `05_SAMPLE_LOADER.md` asks for the
            hot-swap to be a single atomic pointer store, and
            `std::atomic<std::shared_ptr<T>>` is not that: it manipulates a
            reference count under a lock table on every load, which is not
            real-time safe, and Apple's libc++ does not implement it at all.

            Ownership stays on the message thread. `messageThreadCopy` holds the
            buffer the audio thread is reading; `retired` holds the previous one
            until the audio thread has demonstrably finished with it.
        */
        struct Layer
        {
            Layer()
            {
                messageThreadCopy = SampleBuffer::makeEmpty();
                live.store (messageThreadCopy.get(), std::memory_order_release);
            }

            struct RetiredSample
            {
                SampleBufferPtr buffer;
                juce::uint64 retiredAtBlock { 0 };
            };

            std::atomic<const SampleBuffer*> live { nullptr };
            SampleBufferPtr messageThreadCopy;
            std::vector<RetiredSample> retired;
            juce::File file;
            juce::String originalName;
        };

        /** Publishes `next` to the audio thread and retires the previous buffer.
            Message thread only. */
        void publishSample (Layer& layer, SampleBufferPtr next);

        /** Frees a retired buffer once nothing can still be reading it. */
        void releaseRetiredIfSafe (Layer& layer);

        void installFactoryContentAsync();
        void handleAsyncUpdate() override;
        void timerCallback() override;
        void parameterChanged (const juce::String& parameterID, float newValue) override;
        void handleMidiEvent (const juce::MidiMessage& message);
        void applyAuditionCommand() noexcept;
        void applyMidiLearnedController (int ccNumber, int value);
        void updatePerBlockParameters (int numSamples);
        void renderLayers (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
        void cacheParameterPointers();
        void onSampleLoaded (const SampleLoader::LoadResult& result, int layerIndex);
        void reloadSamplesForNewSampleRate();
        void notifyListeners (void (Listener::*callback)(int), int argument);
        void notifySampleMissing (int layerIndex, const juce::String& expectedSha256, const juce::String& fileName);
        void notifyPluginStateChanged();
        float getParameterValue (const juce::String& id, float fallback) const;
        void setParameterValue (const juce::String& id, float value);
        juce::ValueTree captureParameterTree() const;
        void applyParameterTree (const juce::ValueTree& tree);
        std::vector<MutationField> buildMutationFields() const;

        // --- Variation history (F09) ------------------------------------------
        SoundSnapshot captureSoundSnapshot() const;

        /** Applies a complete sound. Refused as a whole if any value is out of range, the
            same rule preset loading uses. Message thread only. */
        bool applySoundSnapshot (const SoundSnapshot& snapshot);

        class SoundSnapshotAction;
        class ModulationCurveAction;
        struct VariationPreviewHost;
        friend class SoundSnapshotAction;
        friend class ModulationCurveAction;
        friend struct VariationPreviewHost;

        VariationList variations;
        VariationPreview<SoundSnapshot> variationPreview;

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

        /** Parameter edit history. Declared before the APVTS, which records into it. */
        juce::UndoManager undoManager;
        juce::AudioProcessorValueTreeState apvts;
        LicenseManager licenseManager;
        PresetManager presetManager;
        // Declared after presetManager so it is destroyed first: queued jobs finish
        // while the preset manager they read from is still alive.
        PortableMediaWorker portableMediaWorker;
        MediaStore mediaStore { MediaStore::defaultDirectory() };
        // Declared before the export controller, so any render job is joined before the
        // cache it finalises into is destroyed.
        RenderedAudioCache renderedAudioCache { RenderedAudioCache::defaultDirectory() };
        ExportController exportController;
        std::optional<RenderedAudio> lastRender;

        struct DragRender
        {
            double sampleRate { 48000.0 };
            int bitDepth { 24 };
        };

        std::optional<DragRender> dragRender;
        WavExporter::Result lastExportResult;
        int exportSerial { 0 };
        MidiLearn midiLearn;
        SampleLoader sampleLoader;
        std::array<std::atomic<bool>, numLayers> sampleLoadMarksDirty {};

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
        std::atomic<float>* tuneParameter { nullptr };
        std::array<std::atomic<float>*, 2> sourceRootParameters {};
        std::array<std::atomic<float>*, 2> sourceFineParameters {};
        std::atomic<float>* reverseParameter { nullptr };
        std::atomic<float>* shimmerParameter { nullptr };
        std::atomic<float>* randomModeParameter { nullptr };
        std::array<std::atomic<float>, 4> followMorphValues {};

        /** F07 tone and space controls, cached like every other parameter. */
        struct ToneSpaceParameters
        {
            std::atomic<float>* mode { nullptr };
            std::atomic<float>* hp { nullptr };
            std::atomic<float>* lp { nullptr };
            std::atomic<float>* filterOn { nullptr };
            std::atomic<float>* tilt { nullptr };
            std::atomic<float>* width { nullptr };
            std::atomic<float>* reverb { nullptr };
            std::atomic<float>* chorus { nullptr };
            std::atomic<float>* drive { nullptr };
        } toneSpaceParameters;

        /** Reads the F07 parameters into the DSP settings struct. Audio thread. */
        ToneSpaceSettings readToneSpaceSettings() const noexcept;

        /** F11 voice and glide parameters, cached like every other parameter. */
        struct PerformanceParameters
        {
            std::atomic<float>* voiceMode { nullptr };
            std::atomic<float>* priority { nullptr };
            std::atomic<float>* glideMs { nullptr };
            std::atomic<float>* glideMode { nullptr };
        } performanceParameters;

        /** Reads the F13 parameters. Audio thread. */
        GrainControlSettings readGrainSettings() const noexcept;

        /** F10 scan and freeze parameters for one layer, cached like every other parameter. */
        struct ScanParameters
        {
            std::atomic<float>* mode { nullptr };
            std::atomic<float>* position { nullptr };
            std::atomic<float>* speed { nullptr };
            std::atomic<float>* freeze { nullptr };
            std::atomic<float>* spread { nullptr };
        };

        std::array<ScanParameters, numLayers> scanParameters {};

        /** Reads one layer's scan settings. Audio thread. */
        ScanSettings readScanSettings (int layerIndex) const noexcept;

        /** F14 modulation parameters, cached like every other parameter. */
        struct ModulationParameters
        {
            std::atomic<float>* shape { nullptr };
            std::atomic<float>* retrigger { nullptr };
            std::atomic<float>* phase { nullptr };
            std::atomic<float>* slewMs { nullptr };
        } modulationParameters;

        /** The curve as the message thread last set it, which undo restores, and the
            mailbox that carries it to the audio thread. The audio thread's own copy is
            touched by nothing else. */
        ModulationCurve lfoCurve;
        CurveMailbox lfoCurveMailbox;
        ModulationCurve lfoCurveAudio;

        /** Keys held across all channels, for First note retrigger. Audio thread only. */
        HeldNoteCounter heldNotes;

        /** Host tempo for the offline renderer's Host retrigger. Written each block. */
        std::atomic<double> lastHostBpm { 120.0 };

        /** Applies a curve without an undo step: undo, redo and A/B recall use it. */
        bool applyModulationCurve (const ModulationCurve& curve);

        /** Reads the F11 parameters into a validated mono configuration. Audio thread. */
        MonoConfig readPerformanceSettings() const noexcept;

        /** F13 grain values and Follow flags, cached like every other parameter. */
        struct GrainParameters
        {
            std::array<std::atomic<float>*, numGrainFields> values {};
            std::array<std::atomic<float>*, numGrainFields> follow {};
        } grainParameters;

        /** Resolves Follow and manual grain values once per block. Audio thread only. */
        GrainTargetResolver grainResolver;

        /** Effective grain values of the last rendered block, for the panel's readouts. */
        std::array<std::atomic<float>, numGrainFields> grainEffective {};
        std::atomic<bool> grainEffectivePublished { false };
        std::atomic<bool> grainOversubscribedFlag { false };
        std::atomic<std::uint64_t> randomSeed { VersionedRandomStreams::defaultSeed };
        std::atomic<bool> randomEvolutionResetRequested { false };

        /** Bumped by every parameter notification. Gentle Mutate compares it between
            planning and writing so a concurrent edit is rejected, not overwritten. */
        std::atomic<std::uint64_t> parameterRevision { 0 };

        // Editing locks and Gentle Mutate state (message thread only).
        LockPolicy::LockSet editLocks;
        LockHistory lockHistory;
        MutateOptions mutateOptions;
        std::uint64_t mutationOperationCount { 0 };

        // --- Host state -------------------------------------------------------
        double currentSampleRate { 44100.0 };
        int currentBlockSize { 512 };
        bool isPrepared { false };
        int activeLayer { 0 };

        /** Bumped at the end of every processBlock. The sample swap uses it to
            tell when the audio thread can no longer be holding an old pointer. */
        std::atomic<juce::uint64> blocksProcessed { 0 };

        // --- Preset bookkeeping ----------------------------------------------
        juce::String currentPresetName;
        juce::String currentPresetCategory { "Cinematic" };
        juce::File currentPresetFile;
        bool presetDirty { false };
        std::atomic<bool> presetDirtyPending { false };
        std::atomic<bool> suppressPresetDirtyTracking { false };
        std::atomic<unsigned int> deferredSampleNotifications { 0 };
        std::atomic<bool> deferredPluginStateNotification { false };

        // --- A/B --------------------------------------------------------------
        std::array<juce::ValueTree, numCompareSlots> compareSlots;
        std::array<ModulationCurve, numCompareSlots> compareCurves;
        int activeCompareSlot { 0 };

        // --- Metering ---------------------------------------------------------
        AtomicPeak outputPeak;
        std::array<AtomicPeak, 2> channelPeaks;
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

        // Audition is transient message-to-audio state. One packed atomic ensures the
        // audio thread observes a complete root/velocity/chord/enabled configuration.
        std::atomic<std::uint32_t> auditionCommand { 0 };
        std::array<bool, 128> audioAuditionNotes {};
        std::uint32_t audioAuditionCommand { 0 };

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
