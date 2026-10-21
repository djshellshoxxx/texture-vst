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

        /** Voices removed by a mode change fade over this long, so nothing clicks. */
        constexpr float retireMilliseconds = 10.0f;

        /** Velocity moves to a retargeted note over this long (F11). */
        constexpr float velocityRampMilliseconds = 5.0f;
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

        scanController.prepare (preparedSampleRate);
        lastScanSource = nullptr;

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

        monoStack.clear();
        monoVoiceIndex = -1;
        hasLastMonoPitch = false;
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

        // Audition is an internal preview and always uses the polyphonic pool.
        if (! auditionOwned && voiceMode != VoiceMode::poly)
        {
            // A zero-velocity note-on is a note-off; it must never start a note.
            if (velocity <= 0.0f)
                monoNoteOff (midiChannel, midiNote);
            else
                monoNoteOn (midiChannel, midiNote, velocity);

            return;
        }

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

        if (! auditionOwned && voiceMode != VoiceMode::poly)
        {
            monoNoteOff (midiChannel, midiNote);
            return;
        }

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

        if (voiceMode != VoiceMode::poly)
        {
            monoStack.setPedal (midiChannel, isDown);

            if (! isDown)
                reselectMonoLine (false);

            return;
        }

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

        // Panic forgets the line, its keys and its glide memory.
        monoStack.clear();
        monoVoiceIndex = -1;
        hasLastMonoPitch = false;

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

        if (voiceMode != VoiceMode::poly)
        {
            monoStack.removeChannel (midiChannel);
            monoStack.setPedal (midiChannel, false);

            if (monoVoiceIndex >= 0 && ! voices[static_cast<size_t> (monoVoiceIndex)].isActive())
                monoVoiceIndex = -1;

            reselectMonoLine (false);
        }

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

    // =========================================================================
    //  Monophonic line (F11)
    // =========================================================================

    void GranularEngine::setVoiceConfig (const MonoConfig& config)
    {
        glideMode = config.glideMode;
        glideSamples = glideSamplesFor (config.glideMs, preparedSampleRate);

        const auto priorityChanged = config.priority != notePriority;
        notePriority = config.priority;

        if (config.voiceMode != voiceMode)
            changeVoiceMode (config.voiceMode);
        else if (priorityChanged && voiceMode != VoiceMode::poly)
            reselectMonoLine (false);
    }

    int GranularEngine::getMonoLineNote() const noexcept
    {
        if (monoVoiceIndex < 0)
            return -1;

        const auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];
        return voice.isActive() && ! voice.isAuditionOwned() && ! voice.isReleasing() && ! voice.isRetiring()
                   ? voice.getMidiNote()
                   : -1;
    }

    float GranularEngine::getMonoLinePitch() const noexcept
    {
        if (monoVoiceIndex < 0)
            return 0.0f;

        const auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];
        return voice.isActive() ? voice.getPitchSemitones() : 0.0f;
    }

    float GranularEngine::getMonoLineVelocity() const noexcept
    {
        return monoVoiceIndex >= 0 ? voices[static_cast<size_t> (monoVoiceIndex)].getStartVelocity() : 0.0f;
    }

    bool GranularEngine::isLineSounding() const noexcept
    {
        if (monoVoiceIndex < 0)
            return false;

        const auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];
        return voice.isActive() && ! voice.isAuditionOwned() && ! voice.isRetiring() && ! voice.isReleasing();
    }

    bool GranularEngine::lineSoundsKey (const MonoNoteStack::Note& note) const noexcept
    {
        if (monoVoiceIndex < 0)
            return false;

        const auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];
        return voice.isActive() && ! voice.isAuditionOwned() && ! voice.isRetiring()
            && voice.getMidiChannel() == note.channel && voice.getMidiNote() == note.number;
    }

    int GranularEngine::lineVoiceForStart (int midiNote) noexcept
    {
        // Reuse the line's voice whenever nothing else owns it, so a restart keeps one
        // voice and never leaves a stranded tail behind.
        if (monoVoiceIndex >= 0)
        {
            const auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];

            if (! voice.isRetiring() && (! voice.isActive() || ! voice.isAuditionOwned()))
                return monoVoiceIndex;
        }

        return findVoiceToUse (midiNote, false);
    }

    void GranularEngine::monoNoteOn (int midiChannel, int midiNote, float velocity)
    {
        // Copy the previous selection: the stack is about to change under any pointer.
        const auto* previous = monoStack.selected (notePriority);
        const bool hadPrevious = previous != nullptr;
        const auto previousNote = hadPrevious ? *previous : MonoNoteStack::Note {};
        const bool previousHeld = hadPrevious && previousNote.heldCount > 0;
        const bool hadSelection = hadPrevious && lineSoundsKey (previousNote);

        monoStack.press (midiChannel, midiNote, velocity, ++monoPressSequence);
        applyMonoDecision (monoStack.selected (notePriority), hadSelection, previousHeld, true);
    }

    void GranularEngine::monoNoteOff (int midiChannel, int midiNote)
    {
        const auto* previous = monoStack.selected (notePriority);

        if (previous == nullptr)
        {
            monoStack.release (midiChannel, midiNote);
            return;
        }

        const auto previousNote = *previous;
        const bool hadSelection = lineSoundsKey (previousNote);

        // A release with nothing held is ignored and cannot underflow the count.
        if (! monoStack.release (midiChannel, midiNote))
            return;

        const auto* after = monoStack.selected (notePriority);

        // Still sounding the same key (held again, or kept by the pedal): nothing to do.
        if (after != nullptr && after->channel == previousNote.channel && after->number == previousNote.number)
            return;

        applyMonoDecision (after, hadSelection, false, false);
    }

    void GranularEngine::reselectMonoLine (bool pressEvent)
    {
        const auto* after = monoStack.selected (notePriority);
        const bool sounding = isLineSounding();

        if (after == nullptr && ! sounding)
            return;

        if (after != nullptr && sounding && lineSoundsKey (*after))
            return;

        const auto& voice = voices[static_cast<size_t> (monoVoiceIndex >= 0 ? monoVoiceIndex : 0)];
        const bool previousHeld = sounding && monoStack.isHeld (voice.getMidiChannel(), voice.getMidiNote());

        applyMonoDecision (after, sounding, previousHeld, pressEvent);
    }

    void GranularEngine::applyMonoDecision (const MonoNoteStack::Note* after, bool hadSelection,
                                            bool previousHeld, bool pressEvent)
    {
        MonoLineContext context;
        context.mode = voiceMode;
        context.glideMode = glideMode;
        context.hadSelection = hadSelection;
        context.hasSelection = after != nullptr;
        context.pressEvent = pressEvent;
        context.previousHeld = previousHeld;
        context.hasLastPitch = hasLastMonoPitch;

        const auto decision = decideMonoLine (context);

        switch (decision.action)
        {
            case MonoLineDecision::Action::none:
                break;

            case MonoLineDecision::Action::release:
                releaseMonoLine();
                break;

            case MonoLineDecision::Action::retarget:
                if (after != nullptr)
                    retargetMonoLine (*after, decision.glide);
                break;

            case MonoLineDecision::Action::restart:
                if (after != nullptr)
                    restartMonoLine (*after, decision.glide);
                break;
        }
    }

    void GranularEngine::restartMonoLine (const MonoNoteStack::Note& note, bool glide)
    {
        const auto index = lineVoiceForStart (note.number);

        if (index < 0)
            return;

        auto& voice = voices[static_cast<size_t> (index)];
        const bool keepsSound = voice.isActive() && ! voice.isAuditionOwned();

        // Glide starts where the listener is hearing the line now, or from the last
        // played pitch when the line has fallen silent.
        float glideFrom = lastMonoPitch;
        int glideLength = 0;

        if (glide)
        {
            glideFrom = keepsSound ? voice.getPitchSemitones() : lastMonoPitch;
            glideLength = glideSamples;
        }

        // A restart keeps its envelope from the current level (no click). Anything else
        // in the slot, such as an audition note, is cut first.
        if (! keepsSound)
            voice.hardStop();

        voice.setAdsrParameters (attackMs, decayMs, sustainLevel, releaseMs);
        voice.setPitchBendRatio (pitchBendRatio);
        voice.startNote (note.number, note.velocity, note.channel, ++noteCounter,
                         sampleStart.load (std::memory_order_relaxed), false,
                         randomMode, randomSeed, ++evolutionCounter,
                         sourceRootMidiNote, sourceFineCents, glideFrom, glideLength);

        monoVoiceIndex = index;
        lastMonoPitch = static_cast<float> (note.number);
        hasLastMonoPitch = true;
    }

    void GranularEngine::retargetMonoLine (const MonoNoteStack::Note& note, bool glide)
    {
        if (! isLineSounding())
            return;

        voices[static_cast<size_t> (monoVoiceIndex)].retarget (
            note.number, note.velocity, note.channel, ++noteCounter,
            glide ? glideSamples : 0,
            glideSamplesFor (velocityRampMilliseconds, preparedSampleRate));

        lastMonoPitch = static_cast<float> (note.number);
        hasLastMonoPitch = true;
    }

    void GranularEngine::releaseMonoLine() noexcept
    {
        if (monoVoiceIndex < 0)
            return;

        auto& voice = voices[static_cast<size_t> (monoVoiceIndex)];

        if (voice.isActive() && ! voice.isAuditionOwned())
            voice.stopNote();
    }

    void GranularEngine::startPolyVoice (int midiChannel, int midiNote, float velocity, bool sustainHeld)
    {
        const auto index = findVoiceToUse (midiNote, false);

        if (index < 0)
            return;

        auto& voice = voices[static_cast<size_t> (index)];
        voice.hardStop();
        voice.setAdsrParameters (attackMs, decayMs, sustainLevel, releaseMs);
        voice.setPitchBendRatio (pitchBendRatio);
        voice.startNote (midiNote, velocity, midiChannel, ++noteCounter,
                         sampleStart.load (std::memory_order_relaxed), false,
                         randomMode, randomSeed, ++evolutionCounter,
                         sourceRootMidiNote, sourceFineCents);
        voice.setHeldBySustain (sustainHeld);
    }

    void GranularEngine::changeVoiceMode (VoiceMode newMode)
    {
        const auto wasMono = voiceMode != VoiceMode::poly;
        const auto isMono = newMode != VoiceMode::poly;
        const auto retireSamples = glideSamplesFor (retireMilliseconds, preparedSampleRate);

        voiceMode = newMode;

        if (! wasMono && isMono)
        {
            // Poly to mono. Keys already sounding keep their voices: the selected key's
            // voice becomes the line, so no note is struck twice. Everything else fades.
            monoStack.clear();

            for (int channel = 1; channel <= 16; ++channel)
                monoStack.setPedal (channel, sustainPedalsDown[static_cast<size_t> (channel - 1)]);

            for (const auto& voice : voices)
            {
                if (! voice.isActive() || voice.isAuditionOwned() || voice.isRetiring())
                    continue;

                const auto sequence = static_cast<std::uint64_t> (voice.getNoteStamp());

                if (voice.isHeldBySustain())
                    monoStack.insertSustained (voice.getMidiChannel(), voice.getMidiNote(),
                                               voice.getStartVelocity(), sequence);
                else if (! voice.isReleasing())
                    monoStack.press (voice.getMidiChannel(), voice.getMidiNote(),
                                     voice.getStartVelocity(), sequence);
            }

            monoVoiceIndex = -1;
            const auto* selected = monoStack.selected (notePriority);
            const auto key = selected != nullptr ? *selected : MonoNoteStack::Note {};

            if (selected != nullptr)
            {
                for (int i = 0; i < maxVoices; ++i)
                {
                    const auto& voice = voices[static_cast<size_t> (i)];

                    if (voice.isActive() && ! voice.isAuditionOwned() && ! voice.isRetiring()
                        && voice.getMidiChannel() == key.channel && voice.getMidiNote() == key.number
                        && (monoVoiceIndex < 0 || voice.getNoteStamp() > voices[static_cast<size_t> (monoVoiceIndex)].getNoteStamp()))
                    {
                        monoVoiceIndex = i;
                    }
                }
            }

            for (int i = 0; i < maxVoices; ++i)
            {
                auto& voice = voices[static_cast<size_t> (i)];

                if (voice.isActive() && ! voice.isAuditionOwned() && ! voice.isRetiring() && i != monoVoiceIndex)
                    voice.beginRetire (retireSamples);
            }

            if (monoVoiceIndex >= 0)
                voices[static_cast<size_t> (monoVoiceIndex)].setHeldBySustain (false);
            else if (selected != nullptr)
                restartMonoLine (key, false);
        }
        else if (wasMono && ! isMono)
        {
            // Mono to poly. The line voice stays as an ordinary voice. Keys still held in
            // the stack, other than the one already sounding, start as poly notes.
            const auto lineVoice = monoVoiceIndex;

            for (int i = 0; i < monoStack.size(); ++i)
            {
                const auto note = monoStack.at (i);
                const bool isLine = lineVoice >= 0
                                 && voices[static_cast<size_t> (lineVoice)].isActive()
                                 && voices[static_cast<size_t> (lineVoice)].getMidiChannel() == note.channel
                                 && voices[static_cast<size_t> (lineVoice)].getMidiNote() == note.number;

                if (isLine)
                    voices[static_cast<size_t> (lineVoice)].setHeldBySustain (note.heldCount == 0);
                else
                    startPolyVoice (note.channel, note.number, note.velocity, note.heldCount == 0);
            }

            monoStack.clear();
            monoVoiceIndex = -1;
        }
        // Legato and retrigger share one allocation rule: the line simply carries on.
    }

    int GranularEngine::renderNextBlock (const SampleBuffer* source,
                                         const MorphTargets& targets,
                                         juce::AudioBuffer<float>& output,
                                         int startSample,
                                         int numSamples,
                                         const RhythmSpan* rhythm) noexcept
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

        // Scan and freeze (F10). A new sample releases any freeze, as a saved position would.
        if (source != lastScanSource)
        {
            scanController.reset (scanSettings.position);
            lastScanSource = source;
        }

        const auto legacyCentre = static_cast<double> (displayReadPosition.load (std::memory_order_relaxed));
        const auto scan = scanController.advance (scanSettings, numSamples,
                                                  static_cast<double> (source->getNumSamples()),
                                                  legacyCentre, targets.positionJitter);

        if (scan.latched)
        {
            freezeLatchValue.store (scan.latchedCentre, std::memory_order_relaxed);
            freezeLatchPending.store (true, std::memory_order_release);
        }

        MorphTargets scanned;
        const MorphTargets* effective = &targets;

        if (scan.active)
        {
            scanned = targets;
            scanned.scanCentre = static_cast<float> (scan.centre);
            scanned.positionJitter = scan.jitter;
            effective = &scanned;
        }

        int rendered = 0;
        float newestPosition = displayReadPosition.load (std::memory_order_relaxed);
        juce::int64 newestStamp = -1;

        if (scan.jumped)
            for (auto& voice : voices)
                voice.requestReadJump (static_cast<float> (scan.centre));

        for (auto& voice : voices)
        {
            if (! voice.isActive())
                continue;

            voice.renderAdding (*source, *effective, scratch, leftOut, rightOut, numSamples, rhythm);
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

    int GranularEngine::getActiveGrainCount() const noexcept
    {
        int total = 0;

        for (const auto& voice : voices)
            total += voice.getActiveGrainCount();

        return total;
    }

    std::uint64_t GranularEngine::getGrainStartCount() const noexcept
    {
        std::uint64_t total = 0;

        for (const auto& voice : voices)
            total += voice.getGrainStartCount();

        return total;
    }
}
