#include "ExportPlanner.h"

#include "../Engine/AuditionChords.h"

#include <algorithm>
#include <cmath>

namespace texture
{
    namespace
    {
        constexpr std::array<double, 4> supportedRates { 44100.0, 48000.0, 88200.0, 96000.0 };

        bool isSupportedRate (double rate) noexcept
        {
            for (const auto supported : supportedRates)
                if (std::abs (supported - rate) < 0.5)
                    return true;

            return false;
        }

        void sortEvents (std::vector<ExportPlanner::NoteEvent>& events)
        {
            std::stable_sort (events.begin(), events.end(),
                              [] (const ExportPlanner::NoteEvent& a, const ExportPlanner::NoteEvent& b)
                              {
                                  if (a.sample != b.sample)
                                      return a.sample < b.sample;

                                  // Note-offs first, so a retrigger at the same position starts cleanly.
                                  return ! a.on && b.on;
                              });
        }

        juce::String rangeError (const char* name, double value, double low, double high)
        {
            return juce::String (name) + " must be between " + juce::String (low, 1) + " and "
                 + juce::String (high, 1) + " (got " + juce::String (value, 2) + ").";
        }
    }

    juce::String ExportPlanner::describe (Mode mode)
    {
        switch (mode)
        {
            case Mode::legacyTwoHit:      return "Legacy two hits";
            case Mode::singleNote:        return "Single note";
            case Mode::chord:             return "Chord";
            case Mode::repeatedPhrase:    return "Repeated phrase";
        }

        return "Unknown";
    }

    ExportPlanner::Plan ExportPlanner::planLegacy (double requestedRate, double holdSecondsIn, double tailSecondsIn,
                                                   double gapSecondsIn, double releaseSecondsIn, int note,
                                                   float velocity)
    {
        Plan result;
        const auto sampleRate = requestedRate > 0.0 ? requestedRate : 48000.0;
        const auto hold = juce::jlimit (0.1, 30.0, holdSecondsIn);
        const auto tail = juce::jlimit (0.0, 30.0, tailSecondsIn);
        const auto gap = juce::jlimit (0.0, 5.0, gapSecondsIn);
        const auto releaseSeconds = juce::jlimit (0.01, 8.0, releaseSecondsIn);

        // Layout: [short hit] [gap] [hit + tail]. These expressions are kept verbatim
        // from the original exporter so archived renders stay bit-identical.
        const auto firstHitSeconds = hold + releaseSeconds + 0.5;
        const auto secondHitSeconds = hold + releaseSeconds + tail;
        const auto totalSeconds = juce::jmin (legacyMaxSeconds, firstHitSeconds + gap + secondHitSeconds);

        result.totalSamples = static_cast<juce::int64> (totalSeconds * sampleRate);
        result.durationSeconds = totalSeconds;

        if (result.totalSamples <= 0)
        {
            result.message = "Nothing to render.";
            return result;
        }

        const auto firstNoteOff = static_cast<juce::int64> (hold * sampleRate);
        const auto secondNoteOn = static_cast<juce::int64> ((firstHitSeconds + gap) * sampleRate);
        const auto secondNoteOff = secondNoteOn + static_cast<juce::int64> (hold * sampleRate);

        result.events = {
            { 0, note, velocity, true },
            { firstNoteOff, note, velocity, false },
            { secondNoteOn, note, velocity, true },
            { secondNoteOff, note, velocity, false }
        };

        sortEvents (result.events);
        result.valid = true;
        return result;
    }

    ExportPlanner::Plan ExportPlanner::plan (const Request& request, double releaseSeconds)
    {
        if (request.mode == Mode::legacyTwoHit)
            return planLegacy (request.sampleRate, request.holdSeconds, request.tailSeconds, request.gapSeconds,
                               releaseSeconds, request.note, static_cast<float> (request.velocity) / 127.0f);

        Plan result;

        // Reject, never clamp: an export must mean what the user typed.
        if (! isSupportedRate (request.sampleRate))
        {
            result.message = "Choose 44.1, 48, 88.2 or 96 kHz.";
            return result;
        }

        if (request.note < 0 || request.note > 127)
        {
            result.message = rangeError ("Note", request.note, 0, 127);
            return result;
        }

        if (request.velocity < 1 || request.velocity > 127)
        {
            result.message = rangeError ("Velocity", request.velocity, 1, 127);
            return result;
        }

        if (request.holdSeconds < 0.1 || request.holdSeconds > 30.0)
        {
            result.message = rangeError ("Hold", request.holdSeconds, 0.1, 30.0);
            return result;
        }

        if (request.tailSeconds < 0.0 || request.tailSeconds > 30.0)
        {
            result.message = rangeError ("Tail", request.tailSeconds, 0.0, 30.0);
            return result;
        }

        if (request.repeats < 1 || request.repeats > maxRepeats)
        {
            result.message = "Repeats must be between 1 and " + juce::String (maxRepeats) + ".";
            return result;
        }

        if (request.gapSeconds < 0.0 || request.gapSeconds > 5.0)
        {
            result.message = rangeError ("Gap", request.gapSeconds, 0.0, 5.0);
            return result;
        }

        if (! AuditionChords::isValidShape (request.chordShape))
        {
            result.message = "Choose a chord shape.";
            return result;
        }

        const auto shape = request.mode == Mode::singleNote ? 0 : request.chordShape;
        const auto releaseClamped = juce::jlimit (0.01, 8.0, releaseSeconds);
        const auto hold = request.holdSeconds;
        const auto period = hold + request.gapSeconds;
        const auto repeats = request.mode == Mode::repeatedPhrase ? request.repeats : 1;

        // Total length: every repeat but the last occupies one period; the last hit
        // holds, releases and then runs its tail.
        result.durationSeconds = (repeats - 1) * period + hold + releaseClamped + request.tailSeconds;

        if (result.durationSeconds > maxDurationSeconds)
        {
            result.message = "This render would be " + juce::String (result.durationSeconds, 1)
                           + " s. The limit is " + juce::String (maxDurationSeconds, 0)
                           + " s: shorten the hold, tail or repeats.";
            return result;
        }

        result.totalSamples = static_cast<juce::int64> (result.durationSeconds * request.sampleRate);

        for (int repeat = 0; repeat < repeats; ++repeat)
        {
            const auto startSeconds = repeat * period;
            const auto onSample = static_cast<juce::int64> (startSeconds * request.sampleRate);
            const auto offSample = static_cast<juce::int64> ((startSeconds + hold) * request.sampleRate);

            for (const auto offset : AuditionChords::offsets[static_cast<size_t> (shape)])
            {
                if (offset < 0)
                    continue;

                const auto sounding = request.note + offset;

                if (sounding > 127)
                {
                    result.events.clear();
                    result.message = "The chord goes above MIDI note 127. Choose a lower note.";
                    return result;
                }

                const auto velocity = static_cast<float> (request.velocity) / 127.0f;
                result.events.push_back ({ onSample, sounding, velocity, true });
                result.events.push_back ({ offSample, sounding, velocity, false });
            }
        }

        sortEvents (result.events);
        result.valid = true;
        return result;
    }
}
