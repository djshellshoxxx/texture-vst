#include "MonoNoteStack.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace texture
{
    namespace
    {
        bool validChannel (int channel) noexcept { return channel >= 1 && channel <= 16; }
        bool validNumber (int number) noexcept { return number >= 0 && number <= 127; }

        float clampFloat (float value, float minimum, float maximum) noexcept
        {
            return value < minimum ? minimum : (value > maximum ? maximum : value);
        }
    }

    MonoConfig monoConfigFromParameters (int voiceModeIndex, int priorityIndex,
                                         float glideMilliseconds, int glideModeIndex) noexcept
    {
        MonoConfig config;

        config.voiceMode = voiceModeIndex == 1 ? VoiceMode::monoRetrigger
                         : voiceModeIndex == 2 ? VoiceMode::monoLegato
                                               : VoiceMode::poly;

        config.priority = priorityIndex == 1 ? NotePriority::high
                        : priorityIndex == 2 ? NotePriority::low
                                             : NotePriority::last;

        config.glideMs = std::isfinite (glideMilliseconds) ? clampFloat (glideMilliseconds, 0.0f, 2000.0f) : 0.0f;
        config.glideMode = glideModeIndex == 0 ? GlideMode::always : GlideMode::overlapOnly;
        return config;
    }

    int glideSamplesFor (float glideMilliseconds, double sampleRate) noexcept
    {
        if (! std::isfinite (glideMilliseconds) || glideMilliseconds <= 0.0f || ! (sampleRate > 0.0))
            return 0;

        const auto clamped = std::min (2000.0, static_cast<double> (glideMilliseconds));
        return static_cast<int> (std::floor (clamped * sampleRate / 1000.0 + 0.5));
    }

    int MonoNoteStack::indexOf (int channel, int number) const noexcept
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& note = notes[static_cast<size_t> (i)];

            if (note.channel == channel && note.number == number)
                return i;
        }

        return -1;
    }

    void MonoNoteStack::eraseAt (int index) noexcept
    {
        for (int i = index; i + 1 < count; ++i)
            notes[static_cast<size_t> (i)] = notes[static_cast<size_t> (i + 1)];

        --count;
    }

    bool MonoNoteStack::press (int channel, int number, float velocity, std::uint64_t pressSequence) noexcept
    {
        if (! validChannel (channel) || ! validNumber (number) || ! std::isfinite (velocity))
            return false;

        const auto clampedVelocity = clampFloat (velocity, 0.0f, 1.0f);
        const auto existing = indexOf (channel, number);

        if (existing >= 0)
        {
            // A repeated press of a held key counts up; the newest press sets order and velocity.
            auto& note = notes[static_cast<size_t> (existing)];
            note.heldCount = std::min (maxHeldCount, note.heldCount + 1);
            note.sustained = false;
            note.velocity = clampedVelocity;
            note.pressSequence = pressSequence;
            return true;
        }

        if (count == capacity)
        {
            // Evict the oldest key nothing is holding; only if there is none, the oldest held key.
            int victim = -1;

            for (int pass = 0; pass < 2 && victim < 0; ++pass)
            {
                auto oldest = std::numeric_limits<std::uint64_t>::max();

                for (int i = 0; i < count; ++i)
                {
                    const auto& note = notes[static_cast<size_t> (i)];
                    const bool eligible = pass == 1 || note.heldCount == 0;

                    if (eligible && note.pressSequence < oldest)
                    {
                        oldest = note.pressSequence;
                        victim = i;
                    }
                }
            }

            if (victim < 0)
                return false;

            eraseAt (victim);
        }

        Note note;
        note.channel = channel;
        note.number = number;
        note.velocity = clampedVelocity;
        note.pressSequence = pressSequence;
        note.heldCount = 1;
        note.sustained = false;
        notes[static_cast<size_t> (count++)] = note;
        return true;
    }

    bool MonoNoteStack::release (int channel, int number) noexcept
    {
        if (! validChannel (channel) || ! validNumber (number))
            return false;

        const auto index = indexOf (channel, number);

        if (index < 0)
            return false;   // nothing held under this channel and number: ignored

        auto& note = notes[static_cast<size_t> (index)];

        if (note.heldCount <= 0)
            return false;

        --note.heldCount;

        if (note.heldCount == 0)
        {
            if (pedals[static_cast<size_t> (channel - 1)])
                note.sustained = true;
            else
                eraseAt (index);
        }

        return true;
    }

    void MonoNoteStack::setPedal (int channel, bool isDown) noexcept
    {
        if (! validChannel (channel))
            return;

        pedals[static_cast<size_t> (channel - 1)] = isDown;

        if (isDown)
            return;

        for (int i = count - 1; i >= 0; --i)
        {
            const auto& note = notes[static_cast<size_t> (i)];

            if (note.channel == channel && note.heldCount == 0)
                eraseAt (i);
        }
    }

    bool MonoNoteStack::isPedalDown (int channel) const noexcept
    {
        return validChannel (channel) && pedals[static_cast<size_t> (channel - 1)];
    }

    bool MonoNoteStack::insertSustained (int channel, int number, float velocity,
                                         std::uint64_t pressSequence) noexcept
    {
        if (! validChannel (channel) || ! validNumber (number) || ! std::isfinite (velocity)
            || indexOf (channel, number) >= 0 || count == capacity)
            return false;

        Note note;
        note.channel = channel;
        note.number = number;
        note.velocity = clampFloat (velocity, 0.0f, 1.0f);
        note.pressSequence = pressSequence;
        note.heldCount = 0;
        note.sustained = true;
        notes[static_cast<size_t> (count++)] = note;
        return true;
    }

    void MonoNoteStack::removeChannel (int channel) noexcept
    {
        for (int i = count - 1; i >= 0; --i)
            if (notes[static_cast<size_t> (i)].channel == channel)
                eraseAt (i);
    }

    void MonoNoteStack::clear() noexcept
    {
        count = 0;
        pedals.fill (false);
    }

    const MonoNoteStack::Note* MonoNoteStack::selected (NotePriority priority) const noexcept
    {
        const Note* best = nullptr;

        for (int i = 0; i < count; ++i)
        {
            const auto& candidate = notes[static_cast<size_t> (i)];

            if (candidate.heldCount <= 0 && ! candidate.sustained)
                continue;

            if (best == nullptr)
            {
                best = &candidate;
                continue;
            }

            bool better = false;

            switch (priority)
            {
                case NotePriority::last:
                    better = candidate.pressSequence > best->pressSequence;
                    break;

                case NotePriority::high:
                    better = candidate.number > best->number
                          || (candidate.number == best->number && candidate.pressSequence > best->pressSequence);
                    break;

                case NotePriority::low:
                    better = candidate.number < best->number
                          || (candidate.number == best->number && candidate.pressSequence > best->pressSequence);
                    break;
            }

            if (better)
                best = &candidate;
        }

        return best;
    }

    const MonoNoteStack::Note* MonoNoteStack::find (int channel, int number) const noexcept
    {
        const auto index = indexOf (channel, number);
        return index >= 0 ? &notes[static_cast<size_t> (index)] : nullptr;
    }

    bool MonoNoteStack::isHeld (int channel, int number) const noexcept
    {
        const auto* note = find (channel, number);
        return note != nullptr && note->heldCount > 0;
    }

    MonoLineDecision decideMonoLine (const MonoLineContext& context) noexcept
    {
        MonoLineDecision decision;

        if (! context.hasSelection)
        {
            decision.action = context.hadSelection ? MonoLineDecision::Action::release
                                                   : MonoLineDecision::Action::none;
            return decision;
        }

        if (context.mode == VoiceMode::monoLegato && context.hadSelection)
        {
            // Overlap keeps the envelope and source progression. The glide shapes the pitch path.
            decision.action = MonoLineDecision::Action::retarget;
            decision.glide = context.glideMode == GlideMode::always
                          || (context.pressEvent && context.previousHeld);
            return decision;
        }

        // Retrigger always restarts. Legato restarts only after silence, so it starts
        // normally. Glide from silence needs Always.
        decision.action = MonoLineDecision::Action::restart;
        decision.glide = context.glideMode == GlideMode::always
                             ? context.hasLastPitch
                             : (context.pressEvent && context.previousHeld && context.hadSelection);
        return decision;
    }
}
