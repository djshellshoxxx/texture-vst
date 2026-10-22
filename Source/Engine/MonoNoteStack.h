#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace texture
{
    /** Voice allocation mode (F11). Poly keeps the original behaviour and is the default. */
    enum class VoiceMode : int { poly = 0, monoRetrigger = 1, monoLegato = 2 };

    /** Which held note the monophonic line sounds (F11). */
    enum class NotePriority : int { last = 0, high = 1, low = 2 };

    /** Whether glide applies to every mono transition or only to overlapping notes. */
    enum class GlideMode : int { always = 0, overlapOnly = 1 };

    /** Host parameter values after range checks, in the enum orders above. */
    struct MonoConfig
    {
        VoiceMode voiceMode { VoiceMode::poly };
        NotePriority priority { NotePriority::last };
        float glideMs { 0.0f };
        GlideMode glideMode { GlideMode::overlapOnly };
    };

    /** Converts raw parameter values into a valid configuration. Out-of-range or
        non-finite values fall back to the defaults, never to an extreme. */
    MonoConfig monoConfigFromParameters (int voiceModeIndex, int priorityIndex,
                                         float glideMilliseconds, int glideModeIndex) noexcept;

    /** Glide length in samples: round(ms * rate / 1000). Zero means immediate. */
    int glideSamplesFor (float glideMilliseconds, double sampleRate) noexcept;

    /** Bounded, allocation-free stack of held keys for the monophonic line.

        Keys are identified by channel and note number, so channel 2's note-off never
        releases channel 1's key. A repeated press of a key counts up (saturating at
        255) and a matching release counts down; a release with nothing held is ignored,
        so the count can never underflow. With sustain down, a released key stays
        eligible until the pedal rises.

        Pure: no JUCE, no allocation. The engine owns one and touches it only from the
        audio thread.
    */
    class MonoNoteStack
    {
    public:
        static constexpr int capacity = 32;
        static constexpr int maxHeldCount = 255;

        struct Note
        {
            int channel { 1 };
            int number { 0 };
            float velocity { 0.0f };
            std::uint64_t pressSequence { 0 };
            int heldCount { 0 };
            bool sustained { false };
        };

        /** Records a press. A full stack evicts its oldest released key, then its oldest
            held key. Returns false only for invalid input. */
        bool press (int channel, int number, float velocity, std::uint64_t pressSequence) noexcept;

        /** Records a release. Returns true when the stack changed. */
        bool release (int channel, int number) noexcept;

        /** Sustain pedal state for a channel. Pedal up removes the channel's sustained keys. */
        void setPedal (int channel, bool isDown) noexcept;
        bool isPedalDown (int channel) const noexcept;

        /** Adds a key that is already released and held by the pedal (poly to mono adoption). */
        bool insertSustained (int channel, int number, float velocity, std::uint64_t pressSequence) noexcept;

        void removeChannel (int channel) noexcept;
        void clear() noexcept;

        /** The key to sound: highest-priority eligible key, where eligible means physically
            held or retained by the pedal. Null when nothing is eligible. */
        const Note* selected (NotePriority priority) const noexcept;

        const Note* find (int channel, int number) const noexcept;
        bool isHeld (int channel, int number) const noexcept;

        int size() const noexcept { return count; }
        bool empty() const noexcept { return count == 0; }
        const Note& at (int index) const noexcept { return notes[static_cast<size_t> (index)]; }

    private:
        int indexOf (int channel, int number) const noexcept;
        void eraseAt (int index) noexcept;

        std::array<Note, capacity> notes {};
        int count { 0 };
        std::array<bool, 16> pedals {};
    };

    /** What the monophonic line knows about one note event. */
    struct MonoLineContext
    {
        VoiceMode mode { VoiceMode::monoLegato };
        GlideMode glideMode { GlideMode::overlapOnly };
        bool hadSelection { false };      // the line sounded an eligible key before the event
        bool hasSelection { false };      // after the event
        bool pressEvent { false };        // the event was a key press (not a release fallback)
        bool previousHeld { false };      // the previously sounding key is still physically held
        bool hasLastPitch { false };      // the line has played before (Always glide from silence)
    };

    struct MonoLineDecision
    {
        enum class Action { none, release, retarget, restart };
        Action action { Action::none };
        bool glide { false };
    };

    /** Pure rule set for the line (F11):
        - nothing eligible after the event: release, if something was sounding;
        - legato with a sounding key: retarget under the same envelope;
        - otherwise: restart the envelope and source position.
        Glide follows GlideMode: Always glides from the last pitch, Overlap only glides
        when a press lands while the previous key is still held. */
    MonoLineDecision decideMonoLine (const MonoLineContext& context) noexcept;
}
