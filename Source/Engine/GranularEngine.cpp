#include "GranularEngine.h"

#include "../Utility/DspHelpers.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace texture
{
    namespace
    {
        /** Pitch bend range, per 14_MIDI_SPEC.md. */
        constexpr float pitchBendSemitones = 2.0f;
        constexpr int pitchBendCentre = 8192;
    }

    GranularEngine::GranularEngine() = default;

    void GranularEngine::prepare (double sampleRate, int maximumBlockSize)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        preparedBlockSize = juce::jmax (1, maximumBlockSize);

        // Every allocation the engine ever makes happens here (15_PERFORMANCE_TARGETS.md).
        scratch.setSize (2, preparedBlockSize, false, true, true);

        for (auto& voice : voices)
            voice.prepare (preparedSampleRate);

        reset();
    }

    void GranularEngine::releaseResources()
    {
        reset();
        scratch.setSize (0, 0);
    }

    void GranularEngine::reset()
    {
        for (auto& voice : voices)
            voice.hardStop();

        sustainPedalsDown.fill (false);
        pitchBendRatio = 1.0f;
        noteCounter = 0;
        evolutionCounter = 0;
        displayReadPosition.store (0.0f, std::memory_order_relaxed);
        activeVoiceCount.store (0, std::memory_order_relaxed);
        scratch.clear();
    }

    int GranularEngine::findVoiceToUse (int midiNote, bool auditionOwned) noexcept
    {
        juce::ignoreUnused (midiNote);

        // 1. First free voice.
        for (int i = 0; i < maxVoices; ++i)
            if (! voices[static_cast<size_t> (i)].isActive())
                return i;

        // 2. Otherwise steal the oldest voice with the lowest amplitude. Score is
        //    envelope x velocity, tie-broken by age (14_MIDI_SPEC.md).
        const auto hasAuditionVoice = std::any_of (voices.begin(), voices.end(), [] (const Voice& voice)
        {
            return voice.isActive() && voice.isAuditionOwned();
        });

        int victim = -1;
        auto lowestScore = std::numeric_limits<float>::max();
        auto oldestStamp = std::numeric_limits<juce::int64>::max();

        for (int i = 0; i < maxVoices; ++i)
        {
            const auto& voice = voices[static_cast<size_t> (i)];

            // Audition can reuse only its own voices. Host notes prefer reclaiming
            // audition capacity before stealing a physically triggered note.
            if (auditionOwned && ! voice.isAuditionOwned())
                continue;
            if (! auditionOwned && hasAuditionVoice && ! voice.isAuditionOwned())
                continue;

            const auto score = voice.getStealScore();
            const auto voiceStamp = voice.getNoteStamp();

            if (score < lowestScore - 1.0e-6f
                || (std::abs (score - lowestScore) <= 1.0e-6f && voiceStamp < oldestStamp))
            {
                lowestScore = score;
                oldestStamp = voiceStamp;
                victim = i;
            }
        }

        return victim;
    }

    void GranularEngine::noteOn (int midiChannel, int midiNote, float velocity, bool auditionOwned)
    {
        if (midiChannel < 1 || midiChannel > 16 || midiNote < 0 || midiNote > 127)
            return;

        // Re-triggering a sounding note allocates a new voice so chord repeats do not
        // cut the previous note (14_MIDI_SPEC.md).
        const auto index = findVoiceToUse (midiNote, auditionOwned);

        if (index < 0)
            return;
        auto& voice = voices[static_cast<size_t> (index)];

        voice.hardStop();
        voice.setAdsrParameters (attackMs, decayMs, sustainLevel, releaseMs);
        voice.setPitchBendRatio (pitchBendRatio);
        voice.startNote (midiNote, velocity, midiChannel, ++noteCounter,
                         sampleStart.load (std::memory_order_relaxed), auditionOwned,
                         randomMode, randomSeed, ++evolutionCounter,
                         sourceRootMidiNote, sourceFineCents);
    }

    void GranularEngine::noteOff (int midiChannel, int midiNote, bool auditionOwned)
    {
        if (midiChannel < 1 || midiChannel > 16 || midiNote < 0 || midiNote > 127)
            return;

        Voice* oldestMatching = nullptr;
        auto oldestStamp = std::numeric_limits<juce::int64>::max();

        for (auto& voice : voices)
        {
            if (! voice.isActive() || voice.getMidiChannel() != midiChannel
                || voice.getMidiNote() != midiNote
                || voice.isAuditionOwned() != auditionOwned
                || voice.isReleasing() || voice.isHeldBySustain())
                continue;

            if (voice.getNoteStamp() < oldestStamp)
            {
                oldestStamp = voice.getNoteStamp();
                oldestMatching = &voice;
            }
        }

        if (oldestMatching == nullptr)
            return;

        if (sustainPedalsDown[static_cast<size_t> (midiChannel - 1)] && ! auditionOwned)
            oldestMatching->setHeldBySustain (true);
        else
            oldestMatching->stopNote();
    }

    void GranularEngine::setSustainPedal (int midiChannel, bool isDown)
    {
        if (midiChannel < 1 || midiChannel > 16)
            return;

        auto& pedalDown = sustainPedalsDown[static_cast<size_t> (midiChannel - 1)];
        pedalDown = isDown;

        if (isDown)
            return;

        for (auto& voice : voices)
        {
            if (voice.isHeldBySustain() && voice.getMidiChannel() == midiChannel)
            {
                voice.setHeldBySustain (false);
                voice.stopNote();
            }
        }
    }

    void GranularEngine::setPitchBend (int pitchWheelValue)
    {
        const auto normalised = (static_cast<float> (pitchWheelValue - pitchBendCentre))
                              / static_cast<float> (pitchBendCentre);

        pitchBendRatio = dsp::semitonesToRatio (juce::jlimit (-1.0f, 1.0f, normalised) * pitchBendSemitones);

        for (auto& voice : voices)
            voice.setPitchBendRatio (pitchBendRatio);
    }

    void GranularEngine::allNotesOff()
    {
        sustainPedalsDown.fill (false);

        for (auto& voice : voices)
            voice.hardStop();

        activeVoiceCount.store (0, std::memory_order_relaxed);
    }

    void GranularEngine::allNotesOff (int midiChannel)
    {
        if (midiChannel < 1 || midiChannel > 16)
            return;

        sustainPedalsDown[static_cast<size_t> (midiChannel - 1)] = false;

        for (auto& voice : voices)
            if (voice.isActive() && voice.getMidiChannel() == midiChannel)
                voice.hardStop();

        int active = 0;
        for (const auto& voice : voices)
            active += voice.isActive() ? 1 : 0;
        activeVoiceCount.store (active, std::memory_order_relaxed);
    }

    void GranularEngine::allSoundOff()
    {
        allNotesOff();
    }

    void GranularEngine::allSoundOff (int midiChannel)
    {
        allNotesOff (midiChannel);
    }

    void GranularEngine::setAdsrParameters (float newAttackMs, float newDecayMs,
                                            float newSustainLevel, float newReleaseMs)
    {
        attackMs = newAttackMs;
        decayMs = newDecayMs;
        sustainLevel = newSustainLevel;
        releaseMs = newReleaseMs;

        for (auto& voice : voices)
            voice.setAdsrParameters (attackMs, decayMs, sustainLevel, releaseMs);
    }

    void GranularEngine::setSourcePitchCalibration (int rootMidiNote, float fineCents) noexcept
    {
        sourceRootMidiNote = sourcePitch::clampRootNote (rootMidiNote);
        sourceFineCents = sourcePitch::clampFineCents (fineCents);
    }

    int GranularEngine::renderNextBlock (const SampleBuffer* source,
                                         const MorphTargets& targets,
                                         juce::AudioBuffer<float>& output,
                                         int startSample,
                                         int numSamples) noexcept
    {
        if (numSamples <= 0 || output.getNumChannels() < 2)
            return 0;

        // The plugin stays silent until a sample arrives (05_SAMPLE_LOADER.md).
        if (source == nullptr || ! source->isValid())
        {
            activeVoiceCount.store (0, std::memory_order_relaxed);
            return 0;
        }

        if (scratch.getNumSamples() < numSamples)
            return 0;   // prepareToPlay has not run for this block size yet

        auto* leftOut = output.getWritePointer (0, startSample);
        auto* rightOut = output.getWritePointer (1, startSample);

        int rendered = 0;
        float newestPosition = displayReadPosition.load (std::memory_order_relaxed);
        juce::int64 newestStamp = -1;

        for (auto& voice : voices)
        {
            if (! voice.isActive())
                continue;

            voice.renderAdding (*source, targets, scratch, leftOut, rightOut, numSamples);
            ++rendered;

            if (voice.getNoteStamp() > newestStamp)
            {
                newestStamp = voice.getNoteStamp();
                newestPosition = voice.getNormalisedReadPosition();
            }
        }

        displayReadPosition.store (newestPosition, std::memory_order_relaxed);
        activeVoiceCount.store (rendered, std::memory_order_relaxed);

        return rendered;
    }

    int GranularEngine::getActiveVoiceCount() const noexcept
    {
        return activeVoiceCount.load (std::memory_order_relaxed);
    }
}
