#include "MidiLearn.h"

#include "ParameterIDs.h"

#include <utility>

namespace texture
{
    namespace
    {
        constexpr const char* storageKeyName = "midi_mappings";

        /** Sustain, all-notes-off and friends keep their standard meaning. */
        constexpr int reservedControllers[] = { 64, 120, 121, 122, 123, 124, 125, 126, 127 };
    }

    bool MidiLearn::isReservedController (int ccNumber) noexcept
    {
        for (const auto reserved : reservedControllers)
            if (ccNumber == reserved)
                return true;

        return ccNumber < 0 || ccNumber >= numControllers;
    }

    const juce::StringArray& MidiLearn::getMappableParameterIDs()
    {
        static const juce::StringArray ids = []
        {
            juce::StringArray result;

            for (const auto* id : params::allIDs)
                result.add (id);

            return result;
        }();

        return ids;
    }

    MidiLearn::MidiLearn (juce::ApplicationProperties* properties,
                          std::function<double()> clockToUse)
        : applicationProperties (properties),
          clock (std::move (clockToUse))
    {
        for (auto& slot : table)
            slot.store (-1, std::memory_order_relaxed);

        load();
    }

    int MidiLearn::slotForParameter (const juce::String& parameterID) const
    {
        return getMappableParameterIDs().indexOf (parameterID);
    }

    double MidiLearn::nowMs() const
    {
        return clock ? clock() : juce::Time::getMillisecondCounterHiRes();
    }

    void MidiLearn::startLearning (const juce::String& parameterID)
    {
        {
            const juce::ScopedLock lock (learnLock);
            learningParameterID = parameterID;
        }

        pendingController.store (-1, std::memory_order_release);
        learningStartedMs = nowMs();
        learning.store (true, std::memory_order_release);
    }

    void MidiLearn::cancelLearning()
    {
        learning.store (false, std::memory_order_release);
        pendingController.store (-1, std::memory_order_release);
        learningStartedMs = 0.0;

        const juce::ScopedLock lock (learnLock);
        learningParameterID.clear();
    }

    bool MidiLearn::cancelLearningIfTimedOut()
    {
        if (! isLearning())
            return false;

        if (nowMs() - learningStartedMs < learnTimeoutMs)
            return false;

        cancelLearning();
        return true;
    }

    juce::String MidiLearn::getLearningParameterID() const
    {
        const juce::ScopedLock lock (learnLock);
        return learningParameterID;
    }

    int MidiLearn::getMappedSlotForController (int ccNumber) const noexcept
    {
        if (ccNumber < 0 || ccNumber >= numControllers)
            return -1;

        return table[static_cast<size_t> (ccNumber)].load (std::memory_order_relaxed);
    }

    void MidiLearn::requestBinding (int ccNumber) noexcept
    {
        if (! learning.load (std::memory_order_acquire) || isReservedController (ccNumber))
            return;

        int expected = -1;
        pendingController.compare_exchange_strong (expected, ccNumber,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_relaxed);
    }

    bool MidiLearn::applyPendingBinding()
    {
        const auto controller = pendingController.exchange (-1, std::memory_order_acq_rel);

        if (controller < 0)
            return false;

        juce::String parameterID;

        {
            const juce::ScopedLock lock (learnLock);
            parameterID = learningParameterID;
            learningParameterID.clear();
        }

        learning.store (false, std::memory_order_release);
        learningStartedMs = 0.0;

        if (parameterID.isEmpty())
            return false;

        setMapping (controller, parameterID);
        return true;
    }

    void MidiLearn::setMapping (int ccNumber, const juce::String& parameterID)
    {
        if (isReservedController (ccNumber))
            return;

        const auto slot = slotForParameter (parameterID);

        if (slot < 0)
            return;

        // One CC per parameter: drop any previous binding for this parameter
        // in memory, then persist the finished mapping table once.
        for (auto& entry : table)
            if (entry.load (std::memory_order_relaxed) == slot)
                entry.store (-1, std::memory_order_release);

        table[static_cast<size_t> (ccNumber)].store (slot, std::memory_order_release);
        save();
    }

    void MidiLearn::clearMapping (int ccNumber)
    {
        if (ccNumber < 0 || ccNumber >= numControllers)
            return;

        table[static_cast<size_t> (ccNumber)].store (-1, std::memory_order_release);
        save();
    }

    void MidiLearn::clearMappingFor (const juce::String& parameterID)
    {
        const auto slot = slotForParameter (parameterID);

        if (slot < 0)
            return;

        bool changed = false;

        for (auto& entry : table)
        {
            if (entry.load (std::memory_order_relaxed) != slot)
                continue;

            entry.store (-1, std::memory_order_release);
            changed = true;
        }

        if (changed)
            save();
    }

    void MidiLearn::clearAll()
    {
        for (auto& entry : table)
            entry.store (-1, std::memory_order_release);

        save();
    }

    juce::String MidiLearn::getParameterForController (int ccNumber) const
    {
        const auto slot = getMappedSlotForController (ccNumber);

        if (slot < 0)
            return {};

        return getMappableParameterIDs()[slot];
    }

    int MidiLearn::getControllerForParameter (const juce::String& parameterID) const
    {
        const auto slot = slotForParameter (parameterID);

        if (slot < 0)
            return -1;

        for (int cc = 0; cc < numControllers; ++cc)
            if (table[static_cast<size_t> (cc)].load (std::memory_order_relaxed) == slot)
                return cc;

        return -1;
    }

    juce::String MidiLearn::describeMappings() const
    {
        juce::StringArray lines;

        for (int cc = 0; cc < numControllers; ++cc)
        {
            const auto parameterID = getParameterForController (cc);

            if (parameterID.isNotEmpty())
                lines.add ("CC" + juce::String (cc) + " -> " + parameterID);
        }

        return lines.isEmpty() ? juce::String ("(none)") : lines.joinIntoString ("\n");
    }

    void MidiLearn::load()
    {
        auto* store = applicationProperties != nullptr ? applicationProperties->getUserSettings() : nullptr;

        if (store == nullptr)
            return;

        const auto raw = store->getValue (storageKeyName);

        if (raw.isEmpty())
            return;

        juce::StringArray entries;
        entries.addTokens (raw, "|", "");

        for (const auto& entry : entries)
        {
            const auto cc = entry.upToFirstOccurrenceOf ("=", false, false).getIntValue();
            const auto parameterID = entry.fromFirstOccurrenceOf ("=", false, false);
            const auto slot = slotForParameter (parameterID);

            if (slot >= 0 && ! isReservedController (cc))
                table[static_cast<size_t> (cc)].store (slot, std::memory_order_relaxed);
        }
    }

    void MidiLearn::save() const
    {
        auto* store = applicationProperties != nullptr ? applicationProperties->getUserSettings() : nullptr;

        if (store == nullptr)
            return;

        juce::StringArray entries;

        for (int cc = 0; cc < numControllers; ++cc)
        {
            const auto slot = table[static_cast<size_t> (cc)].load (std::memory_order_relaxed);

            if (slot >= 0)
                entries.add (juce::String (cc) + "=" + getMappableParameterIDs()[slot]);
        }

        store->setValue (storageKeyName, entries.joinIntoString ("|"));
        store->saveIfNeeded();
    }
}
