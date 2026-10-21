#pragma once

#include <algorithm>
#include <cmath>

namespace texture::sourcePitch
{
    /** TEXTURE names MIDI 60 as C3 to match the documented sample-root convention. */
    constexpr int minimumMidiNote = 0;
    constexpr int maximumMidiNote = 127;
    constexpr int defaultRootMidiNote = 60;

    constexpr int clampRootNote (int midiNote) noexcept
    {
        return midiNote < minimumMidiNote ? minimumMidiNote
             : midiNote > maximumMidiNote ? maximumMidiNote
             : midiNote;
    }

    inline float clampFineCents (float cents) noexcept
    {
        return ! std::isfinite (cents) ? 0.0f
             : cents < -100.0f ? -100.0f
             : cents > 100.0f ? 100.0f
             : cents;
    }

    /** Returns the playback ratio for a note, preserving the legacy tune control. */
    inline double playbackRatio (int playedMidiNote,
                                 int rootMidiNote,
                                 float tuneSemitones,
                                 float fineCents,
                                 double sourceSampleRate,
                                 double engineSampleRate) noexcept
    {
        const auto tune = std::isfinite (tuneSemitones) ? tuneSemitones : 0.0f;
        const auto cents = clampFineCents (fineCents);
        const auto noteRatio = std::pow (2.0f,
                                         static_cast<float> (playedMidiNote - clampRootNote (rootMidiNote))
                                             * (1.0f / 12.0f));
        const auto tuneRatio = std::pow (2.0f, tune * (1.0f / 12.0f));
        const auto fineRatio = std::pow (2.0f, (cents / 100.0f) * (1.0f / 12.0f));
        // Keep the legacy float multiplication order when the added controls are
        // at their defaults; only the optional source-rate correction is new.
        const auto pitch = static_cast<double> ((noteRatio * tuneRatio) * fineRatio);
        const auto rateRatio = sourceSampleRate > 0.0 && engineSampleRate > 0.0
                                   ? sourceSampleRate / engineSampleRate
                                   : 1.0;
        return std::isfinite (pitch * rateRatio) && pitch * rateRatio > 0.0
                   ? pitch * rateRatio
                   : 1.0;
    }
}
