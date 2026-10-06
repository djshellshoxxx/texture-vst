#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <functional>

namespace texture
{
    /** MIDI CC to parameter mapping, driven by the right-click menu on any control.

        The audio thread only ever reads the table, and it reads it through plain
        atomics, so learning a new mapping from the message thread never blocks
        anything. Mappings persist in ApplicationProperties.
    */
    class MidiLearn
    {
    public:
        static constexpr int numControllers = 128;
        static constexpr double learnTimeoutMs = 15000.0;

        /** Controllers reserved by the MIDI spec that we never map. */
        static bool isReservedController (int ccNumber) noexcept;

        explicit MidiLearn (juce::ApplicationProperties* properties,
                            std::function<double()> clock = {});

        /** Arms learning: the next CC received is bound to `parameterID`. */
        void startLearning (const juce::String& parameterID);

        /** Cancels an armed learn without binding anything. */
        void cancelLearning();

        /** Message thread: cancels learning once the 15-second learn window expires.
            Returns true only when this call performed the timeout cancellation. */
        bool cancelLearningIfTimedOut();

        bool isLearning() const noexcept { return learning.load (std::memory_order_acquire); }

        juce::String getLearningParameterID() const;

        /** Called from the audio thread for every CC message.

            Returns the parameter ID to update, or an empty string. When a learn is
            armed the binding is recorded as a request for the message thread, which
            is what keeps this function allocation-free.
        */
        int getMappedSlotForController (int ccNumber) const noexcept;

        /** Records that `ccNumber` should be bound; consumed on the message thread. */
        void requestBinding (int ccNumber) noexcept;

        /** Message thread: completes any pending binding. Returns true if one landed. */
        bool applyPendingBinding();

        // --- Table access (message thread) -----------------------------------
        void setMapping (int ccNumber, const juce::String& parameterID);
        void clearMapping (int ccNumber);
        void clearMappingFor (const juce::String& parameterID);
        void clearAll();

        juce::String getParameterForController (int ccNumber) const;
        int getControllerForParameter (const juce::String& parameterID) const;

        /** Human-readable summary for the debug and troubleshooting reports. */
        juce::String describeMappings() const;

        /** Parameter IDs in the order the audio thread indexes them. */
        static const juce::StringArray& getMappableParameterIDs();

    private:
        void load();
        void save() const;
        int slotForParameter (const juce::String& parameterID) const;
        double nowMs() const;

        juce::ApplicationProperties* applicationProperties { nullptr };
        std::function<double()> clock;
        double learningStartedMs { 0.0 };

        /** ccNumber -> slot index into getMappableParameterIDs, or -1. */
        std::array<std::atomic<int>, numControllers> table {};

        std::atomic<bool> learning { false };
        std::atomic<int> pendingController { -1 };

        juce::CriticalSection learnLock;
        juce::String learningParameterID;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearn)
    };
}
