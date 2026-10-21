#include "Voice.h"

#include "../Utility/DspHelpers.h"

#include <cmath>

namespace texture
{
    namespace
    {
        /** Velocity maps linearly to amplitude over this range (14_MIDI_SPEC.md). */
        constexpr float velocityFloor = 0.3f;
        constexpr float velocityCeiling = 1.0f;

        /** Below this envelope level a released voice stops spawning grains
            (04_SYNTH_ENGINE.md: no new grains once the voice falls under -60 dB).
        */
        constexpr float grainCutoffLevel = dsp::minusSixtyDb;

        /** Glide pitch is updated at this granularity. 16 samples is 0.3 ms at 48 kHz, and
            the pitch steps inside it are far below audibility for any glide time. */
        constexpr int glideChunkSamples = 16;
    }

    void Voice::prepare (double sampleRate)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        envelope.prepare (preparedSampleRate);
        fineTuneCents.reset (preparedSampleRate, 0.02);
        reset();
    }

    void Voice::reset()
    {
        for (auto& grain : grains)
            grain.stop();

        envelope.reset();

        active = false;
        sustainHeld = false;
        auditionOwned = false;
        currentNote = -1;
        currentChannel = 1;
        currentSourceRootMidiNote = sourcePitch::defaultRootMidiNote;
        stamp = 0;
        velocityGain = 1.0f;
        velocityTarget = 1.0f;
        velocityStep = 0.0f;
        velocityRampRemaining = 0;
        startVelocity = 0.0f;
        glideOffset = 0.0f;
        glideStartOffset = 0.0f;
        glideTotal = 0;
        glideRemaining = 0;
        grainPitchDirty = false;
        retireTotal = 0;
        retireRemaining = 0;
        currentTuneSemitones = 0.0f;
        fineTuneForCurrentBlock = 0.0f;
        lastEnvelopeLevel = 0.0f;
        normalisedReadPosition = 0.0f;
        readPosition = 0.0;
        samplesUntilNextGrain = 0.0;
    }

    void Voice::startNote (int midiNote, float velocity, int midiChannel, juce::int64 noteStamp,
                           float startNormalised, bool isAuditionOwned,
                           int newRandomMode, std::uint64_t newRandomSeed,
                           std::uint64_t randomNoteEvent,
                           int sourceRootMidiNote, float sourceFineCents,
                           float glideFromSemitones, int glideSamples)
    {
        for (auto& grain : grains)
            grain.stop();

        currentNote = midiNote;
        currentChannel = midiChannel;
        auditionOwned = isAuditionOwned;
        stamp = noteStamp;
        sustainHeld = false;
        randomMode = juce::jlimit (0, 2, newRandomMode);
        randomSeed = newRandomSeed ^ (static_cast<std::uint64_t> (midiNote) * UINT64_C (0xD6E8FEB86659FD93));
        randomEvent = randomMode == 2 ? randomNoteEvent : 0;
        randomDrawCounts.fill (0);

        const auto clampedVelocity = juce::jlimit (0.0f, 1.0f, velocity);
        velocityGain = juce::jmap (clampedVelocity, velocityFloor, velocityCeiling);
        velocityTarget = velocityGain;
        velocityStep = 0.0f;
        velocityRampRemaining = 0;
        startVelocity = clampedVelocity;

        // Glide starts at the previous pitch; without a glide the offset is zero.
        glideTotal = glideSamples > 0 ? glideSamples : 0;
        glideRemaining = glideTotal;
        glideOffset = glideTotal > 0 ? glideFromSemitones - static_cast<float> (midiNote) : 0.0f;
        glideStartOffset = glideOffset;
        grainPitchDirty = false;

        retireTotal = 0;
        retireRemaining = 0;

        currentSourceRootMidiNote = sourcePitch::clampRootNote (sourceRootMidiNote);
        currentTuneSemitones = 0.0f;
        const auto initialFineCents = sourcePitch::clampFineCents (sourceFineCents);
        fineTuneCents.setCurrentAndTargetValue (initialFineCents);
        fineTuneForCurrentBlock = initialFineCents;

        readPosition = 0.0;
        pendingStart = juce::jlimit (0.0f, 1.0f, startNormalised);
        startPending = pendingStart > 0.0f;
        samplesUntilNextGrain = 0.0;
        normalisedReadPosition = pendingStart;
        lastEnvelopeLevel = 0.0f;

        // Deterministic within a note, varied across notes (06_MORPH_KNOB_BEHAVIOUR.md).
        voiceRandom.setSeed (static_cast<juce::int64> (midiNote) * 7919 + noteStamp * 104729 + 1);

        envelope.noteOn();
        active = true;
    }

    void Voice::retarget (int midiNote, float velocity, int midiChannel, juce::int64 noteStamp,
                          int glideSamples, int velocityRampSamples)
    {
        if (! active)
            return;

        // The pitch the listener hears right now is the starting point of any glide.
        const auto pitchNow = getPitchSemitones();

        currentNote = midiNote;
        currentChannel = midiChannel;
        stamp = noteStamp;
        sustainHeld = false;

        const auto clampedVelocity = juce::jlimit (0.0f, 1.0f, velocity);
        startVelocity = clampedVelocity;
        velocityTarget = juce::jmap (clampedVelocity, velocityFloor, velocityCeiling);

        if (velocityRampSamples > 0)
        {
            velocityRampRemaining = velocityRampSamples;
            velocityStep = (velocityTarget - velocityGain) / static_cast<float> (velocityRampSamples);
        }
        else
        {
            velocityGain = velocityTarget;
            velocityRampRemaining = 0;
        }

        if (glideSamples > 0)
        {
            glideOffset = pitchNow - static_cast<float> (midiNote);
            glideStartOffset = glideOffset;
            glideTotal = glideSamples;
            glideRemaining = glideSamples;
        }
        else
        {
            glideOffset = 0.0f;
            glideStartOffset = 0.0f;
            glideTotal = 0;
            glideRemaining = 0;
        }

        // Grains already sounding must follow the new note, with or without a glide.
        grainPitchDirty = true;
    }

    void Voice::stopNote()
    {
        if (! active)
            return;

        envelope.noteOff();
    }

    void Voice::hardStop()
    {
        for (auto& grain : grains)
            grain.stop();

        envelope.reset();
        active = false;
        sustainHeld = false;
        currentNote = -1;
        lastEnvelopeLevel = 0.0f;
        glideOffset = 0.0f;
        glideStartOffset = 0.0f;
        glideTotal = 0;
        glideRemaining = 0;
        grainPitchDirty = false;
        velocityRampRemaining = 0;
        retireTotal = 0;
        retireRemaining = 0;
    }

    void Voice::beginRetire (int fadeSamples) noexcept
    {
        if (! active)
            return;

        retireTotal = juce::jmax (1, fadeSamples);
        retireRemaining = retireTotal;
    }

    void Voice::setAdsrParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs)
    {
        envelope.setParameters (attackMs, decayMs, sustainLevel, releaseMs);
    }

    int Voice::findFreeGrainSlot() noexcept
    {
        for (int i = 0; i < maxGrains; ++i)
            if (! grains[static_cast<size_t> (i)].isActive())
                return i;

        return -1;
    }

    float Voice::nextRandom (VersionedRandomStreams::Lane lane) noexcept
    {
        if (randomMode == params::randomModeDefault)
            return voiceRandom.nextFloat();

        std::size_t laneIndex = 0;
        switch (lane)
        {
            case VersionedRandomStreams::Lane::grainPosition: laneIndex = 0; break;
            case VersionedRandomStreams::Lane::shimmer:       laneIndex = 1; break;
            case VersionedRandomStreams::Lane::reverse:       laneIndex = 2; break;
            case VersionedRandomStreams::Lane::detune:        laneIndex = 3; break;
            case VersionedRandomStreams::Lane::pan:           laneIndex = 4; break;
            case VersionedRandomStreams::Lane::modulation:    laneIndex = 5; break;
            case VersionedRandomStreams::Lane::effects:        laneIndex = 6; break;

            // Gentle Mutate's lane belongs to the planner. A voice never draws from it.
            case VersionedRandomStreams::Lane::mutation:       return voiceRandom.nextFloat();
        }

        const auto draw = randomDrawCounts[laneIndex]++;
        return VersionedRandomStreams::unitFloat (randomSeed, lane, randomEvent, draw);
    }

    void Voice::spawnGrain (const SampleBuffer& source,
                            const MorphTargets& targets,
                            int grainDurationSamples,
                            float taper,
                            float glideMultiplier) noexcept
    {
        const auto slot = findFreeGrainSlot();

        if (slot < 0)
            return;

        const auto sourceLength = source.getNumSamples();

        if (sourceLength <= 0 || grainDurationSamples <= 0)
            return;

        // 1. Start position: voice read head plus jitter proportional to MORPH.
        auto start = readPosition;

        if (targets.positionJitter > 0.0f)
        {
            const auto jitterRange = static_cast<double> (targets.positionJitter)
                                   * static_cast<double> (sourceLength);
            const auto jitterValue = randomMode == params::randomModeDefault
                ? voiceRandom.nextDouble()
                : static_cast<double> (nextRandom (VersionedRandomStreams::Lane::grainPosition));
            start += (jitterValue * 2.0 - 1.0) * jitterRange;
        }

        // 2. Pitch: voice pitch plus a per-grain detune of +/- MORPH * 30 cents.
        const auto basePitch = static_cast<float> (sourcePitch::playbackRatio (
            currentNote, currentSourceRootMidiNote, currentTuneSemitones,
            fineTuneForCurrentBlock, source.getSampleRate(), preparedSampleRate));
        auto ratio = basePitch * pitchBendRatio;
        auto amplitudeScale = 1.0f;

        // The random draws below are only taken when their feature is on, so a
        // patch with both at zero renders bit-identically to earlier versions.
        // SHIMMER: some grains jump an octave up, quieter, for a bright halo.
        if (targets.shimmerAmount > 0.0f
            && nextRandom (VersionedRandomStreams::Lane::shimmer) < dsp::clamp (targets.shimmerAmount, 0.0f, 1.0f) * 0.5f)
        {
            ratio *= 2.0f;
            amplitudeScale = 0.7f;
        }

        // REVERSE: some grains read backwards.
        const auto reversed = targets.reverseChance > 0.0f
                              && nextRandom (VersionedRandomStreams::Lane::reverse) < dsp::clamp (targets.reverseChance, 0.0f, 1.0f);

        if (targets.detuneCents > 0.0f)
        {
            const auto cents = (nextRandom (VersionedRandomStreams::Lane::detune) * 2.0f - 1.0f) * targets.detuneCents;
            ratio *= dsp::centsToRatio (cents);
        }

        // Glide: only a voice that is gliding carries a multiplier other than one.
        if (! juce::exactlyEqual (glideMultiplier, 1.0f))
            ratio *= glideMultiplier;

        grainPitchFactor[static_cast<size_t> (slot)] = ratio / (basePitch * glideMultiplier);

        // 3. Pan: centred at MORPH=0, widening to +/- 0.8 at MORPH=1.
        const auto pan = targets.panSpread > 0.0f
            ? (nextRandom (VersionedRandomStreams::Lane::pan) * 2.0f - 1.0f) * targets.panSpread
            : 0.0f;

        // Overlapping grains sum, so scale back by the incoherent-summation factor
        // to keep perceived loudness steady as MORPH opens the grain count up.
        const auto amplitude = amplitudeScale
                             / std::sqrt (static_cast<float> (juce::jmax (1, targets.grainsPerVoice)));

        grains[static_cast<size_t> (slot)].start (start, grainDurationSamples, ratio, pan, amplitude, taper, reversed);
        grainStarts.fetch_add (1, std::memory_order_relaxed);
    }

    int Voice::getActiveGrainCount() const noexcept
    {
        int count = 0;

        for (const auto& grain : grains)
            if (grain.isActive())
                ++count;

        return count;
    }

    void Voice::updateGrainPitch (float basePitch, float multiplier) noexcept
    {
        for (size_t i = 0; i < grains.size(); ++i)
            if (grains[i].isActive())
                grains[i].setPlaybackRatio (grainPitchFactor[i] * basePitch * multiplier);
    }

    void Voice::renderSpan (const SampleBuffer& source, const MorphTargets& targets,
                            int durationSamples, float taper, double hop, bool canSpawn,
                            float* scratchLeft, float* scratchRight,
                            int begin, int end, float multiplier) noexcept
    {
        int offset = begin;

        while (offset < end)
        {
            if (samplesUntilNextGrain <= 0.0)
            {
                if (canSpawn)
                    spawnGrain (source, targets, durationSamples, taper, multiplier);

                samplesUntilNextGrain += hop;

                // Guard against a pathological hop that would not advance the loop.
                if (samplesUntilNextGrain <= 0.0)
                    samplesUntilNextGrain = hop;
            }

            const auto remaining = end - offset;
            const auto untilSpawn = juce::jmax (1, static_cast<int> (std::ceil (samplesUntilNextGrain)));
            const auto segment = juce::jmin (remaining, untilSpawn);

            for (auto& grain : grains)
                if (grain.isActive())
                    grain.renderAdding (source, scratchLeft + offset, scratchRight + offset, segment);

            samplesUntilNextGrain -= static_cast<double> (segment);
            offset += segment;
        }
    }

    void Voice::renderAdding (const SampleBuffer& source,
                              const MorphTargets& targets,
                              juce::AudioBuffer<float>& scratch,
                              float* leftOut,
                              float* rightOut,
                              int numSamples) noexcept
    {
        if (! active || numSamples <= 0 || leftOut == nullptr || rightOut == nullptr)
            return;

        const auto sourceLength = source.getNumSamples();

        if (sourceLength <= 0 || scratch.getNumChannels() < 2 || scratch.getNumSamples() < numSamples)
            return;

        if (startPending)
        {
            startPending = false;
            readPosition = static_cast<double> (pendingStart) * static_cast<double> (sourceLength - 1);
        }

        auto* scratchLeft = scratch.getWritePointer (0);
        auto* scratchRight = scratch.getWritePointer (1);

        juce::FloatVectorOperations::clear (scratchLeft, numSamples);
        juce::FloatVectorOperations::clear (scratchRight, numSamples);

        // Grain duration and hop, derived once per block from MORPH.
        const auto durationSamples = juce::jmax (
            8, juce::roundToInt (targets.grainDurationMs * 0.001f * preparedSampleRate));

        // Legacy cadence: the hop follows duration and overlap. A manual grain density (F13)
        // spawns at a fixed interval instead, and its overlap was already derived from it.
        const auto overlap = dsp::clamp (targets.grainOverlap, 0.0f, 0.95f);
        const auto densityManual = std::isfinite (targets.grainDensityHz) && targets.grainDensityHz > 0.0f;
        const auto hop = densityManual
            ? juce::jmax (1.0, preparedSampleRate / static_cast<double> (targets.grainDensityHz))
            : juce::jmax (1.0, static_cast<double> (durationSamples) * (1.0 - overlap));

        // The taper tracks the overlap: a lone grain gets a near-rectangular window
        // (clean playback), a fully overlapped swarm gets a true Hann.
        const auto taper = juce::jlimit (Grain::minimumTaper, 1.0f, overlap > 0.0f ? overlap : Grain::minimumTaper);

        // TUNE: whole semitones, so a sample recorded away from C3 can be put back
        // in key. Clamped here as well because the fuzzer feeds raw targets.
        currentTuneSemitones = std::isfinite (targets.tuneSemitones)
                                   ? dsp::clamp (targets.tuneSemitones,
                                                 params::tuneMinSemitones,
                                                 params::tuneMaxSemitones)
                                   : 0.0f;
        fineTuneCents.setTargetValue (sourcePitch::clampFineCents (targets.sourceFineCents));
        const auto fineAtBlockStart = fineTuneCents.getNextValue();
        fineTuneForCurrentBlock = fineAtBlockStart;
        if (numSamples > 1)
            fineTuneCents.skip (numSamples - 1);
        const auto readPitchRatio = static_cast<float> (sourcePitch::playbackRatio (
            currentNote, currentSourceRootMidiNote, currentTuneSemitones, fineAtBlockStart,
            source.getSampleRate(), preparedSampleRate));
        const auto readIncrement = static_cast<double> (readPitchRatio * pitchBendRatio);
        const auto sourceLengthAsDouble = static_cast<double> (sourceLength);
        // Spawning must stop the moment the envelope finishes. `isInReleasePhase()`
        // goes false again once the release completes, so testing it alone would
        // re-open the gate on a dead voice and the voice would never retire.
        const auto canSpawn = envelope.isActive()
                              && (! envelope.isInReleasePhase() || lastEnvelopeLevel > grainCutoffLevel);

        // A glide or a retarget needs the grains updated, so the block is cut into short
        // chunks while that happens. A steady voice is one chunk, exactly as before.
        const auto glideOrJump = glideRemaining > 0 || grainPitchDirty;
        int chunkStart = 0;

        while (chunkStart < numSamples)
        {
            const auto gliding = glideRemaining > 0;
            const auto chunkEnd = (glideOrJump && gliding)
                                      ? juce::jmin (numSamples, chunkStart + glideChunkSamples)
                                      : numSamples;

            const auto multiplier = gliding ? dsp::semitonesToRatio (glideOffset) : 1.0f;

            if (glideOrJump)
            {
                updateGrainPitch (readPitchRatio, multiplier);
                grainPitchDirty = false;
            }

            renderSpan (source, targets, durationSamples, taper, hop, canSpawn,
                        scratchLeft, scratchRight, chunkStart, chunkEnd, multiplier);

            const auto chunkLength = chunkEnd - chunkStart;

            // Advance the voice read head at the playback rate so that at MORPH=0 each
            // grain picks up exactly where the previous one ended.
            const auto chunkIncrement = juce::exactlyEqual (multiplier, 1.0f)
                                            ? readIncrement
                                            : readIncrement * static_cast<double> (multiplier);
            readPosition += chunkIncrement * static_cast<double> (chunkLength);

            if (readPosition >= sourceLengthAsDouble || readPosition < 0.0)
                readPosition -= sourceLengthAsDouble * std::floor (readPosition / sourceLengthAsDouble);

            if (gliding)
            {
                glideRemaining = juce::jmax (0, glideRemaining - chunkLength);
                glideOffset = glideRemaining > 0
                                  ? glideStartOffset * static_cast<float> (glideRemaining) / static_cast<float> (glideTotal)
                                  : 0.0f;
            }

            chunkStart = chunkEnd;
        }

        normalisedReadPosition = static_cast<float> (readPosition / sourceLengthAsDouble);

        // Apply the amplitude envelope and mix into the engine output.
        float envelopeLevel = lastEnvelopeLevel;

        if (velocityRampRemaining == 0 && retireTotal == 0)
        {
            const auto voiceGain = velocityGain;

            for (int i = 0; i < numSamples; ++i)
            {
                envelopeLevel = envelope.getNextSample();
                const auto gain = envelopeLevel * voiceGain;

                leftOut[i]  += scratchLeft[i] * gain;
                rightOut[i] += scratchRight[i] * gain;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                envelopeLevel = envelope.getNextSample();

                if (velocityRampRemaining > 0)
                {
                    velocityGain += velocityStep;

                    if (--velocityRampRemaining == 0)
                        velocityGain = velocityTarget;
                }

                float retireLevel = 1.0f;

                if (retireTotal > 0)
                {
                    retireLevel = static_cast<float> (retireRemaining) / static_cast<float> (retireTotal);

                    if (retireRemaining > 0)
                        --retireRemaining;
                }

                const auto gain = envelopeLevel * velocityGain * retireLevel;

                leftOut[i]  += scratchLeft[i] * gain;
                rightOut[i] += scratchRight[i] * gain;
            }
        }

        lastEnvelopeLevel = envelopeLevel;

        if (retireTotal > 0 && retireRemaining <= 0)
        {
            hardStop();
            return;
        }

        if (! envelope.isActive())
        {
            bool anyGrainActive = false;

            for (const auto& grain : grains)
                anyGrainActive = anyGrainActive || grain.isActive();

            if (! anyGrainActive)
            {
                active = false;
                sustainHeld = false;
                currentNote = -1;
                lastEnvelopeLevel = 0.0f;
            }
        }
    }
}
