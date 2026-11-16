#pragma once

#include <cstddef>

namespace texture
{
    /** The preview state machine for variations (F09). Pure: the host supplies the
        sound operations, so the rules can be tested without a processor.

        - Starting a preview remembers the sound as it was before the first preview,
          then applies the target.
        - Moving to another variation cancels the current preview first, so the restore
          point never moves to an intermediate sound.
        - Cancel restores exactly the remembered sound.
        - Accept keeps whatever sound is current, and ends the preview.

        Host requirements: captureState(), applyPreview(const Snapshot&) and
        restorePreview(const Snapshot&).
    */
    template <typename Snapshot>
    class VariationPreview
    {
    public:
        bool isActive() const noexcept { return active; }
        std::size_t getActiveIndex() const noexcept { return activeIndex; }

        template <typename Host>
        void begin (Host& host, std::size_t targetIndex, const Snapshot& target)
        {
            if (active)
                cancel (host);

            remembered = host.captureState();
            host.applyPreview (target);
            activeIndex = targetIndex;
            active = true;
        }

        template <typename Host>
        void cancel (Host& host)
        {
            if (! active)
                return;

            host.restorePreview (remembered);
            active = false;
        }

        /** Keeps the current sound. Changes nothing audible. */
        void accept() noexcept
        {
            active = false;
        }

        /** Forgets the preview without touching the sound: used when the host state is
            replaced, because the undo history it depended on has been cleared. */
        void reset() noexcept
        {
            active = false;
        }

        const Snapshot& getRemembered() const noexcept { return remembered; }

    private:
        Snapshot remembered {};
        std::size_t activeIndex { 0 };
        bool active { false };
    };
}
