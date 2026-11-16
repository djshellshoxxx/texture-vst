#pragma once

#include "../Utility/ParameterIDs.h"

namespace texture
{
    /** Independent tone and space controls (F07). Plain values: the processor converts
        host parameters into this struct once per block, and the tests build it directly. */
    struct ToneSpaceSettings
    {
        bool independent { false };
        float hpHz { params::toneHpDefaultHz };
        float lpHz { params::toneLpDefaultHz };
        bool filterOn { params::toneFilterOnDefault };
        float tiltDb { params::toneTiltDefaultDb };
        float width { params::spaceWidthDefault };
        float reverbSend { params::spaceReverbDefault };
        float chorusAmount { params::spaceChorusDefault };
        float drive { params::toneDriveDefault };
    };

    /** Returns a copy with every field finite and inside its documented range.
        A non-finite value falls back to its default, never to an extreme. */
    ToneSpaceSettings sanitiseToneSpace (const ToneSpaceSettings& settings) noexcept;

    /** The effective values the chain renders with for one block. */
    struct ResolvedToneSpace
    {
        float saturatorDrive { 0.0f };
        float chorusDepth { 0.0f };
        float reverbSend { 0.0f };
        float highShelfTiltDb { 0.0f };
        float filterMix { 0.0f };    // 0 = bypassed, 1 = fully filtered
        float hpHz { params::toneHpDefaultHz };
        float lpHz { params::toneLpDefaultHz };
        float width { 1.0f };
    };

    /** Chooses between MORPH-derived and independent values, and ramps between them.

        Follow MORPH returns the MORPH targets exactly, with no smoothing, so legacy
        patches render as they always did. A mode change, or any change while
        Independent is active, is ramped over `transitionSeconds`; once a transition
        completes it snaps to the goal, so the settled output carries no residue.

        Pure: no JUCE audio objects, no allocation after prepare(). */
    class ToneSpaceRouter
    {
    public:
        static constexpr double transitionSeconds = 0.02;

        void prepare (double sampleRate) noexcept;

        /** Settles on the Follow MORPH values, with no transition pending. */
        void reset (const MorphTargets& morph) noexcept;

        /** Advances by one block of `numSamples` and returns the values to render with. */
        ResolvedToneSpace advance (const MorphTargets& morph,
                                   const ToneSpaceSettings& settings,
                                   int numSamples) noexcept;

        /** Pure mapping from settings to the goal values, without ramping. */
        static ResolvedToneSpace resolve (const MorphTargets& morph,
                                          const ToneSpaceSettings& settings) noexcept;

        bool isTransitioning() const noexcept { return transitionRemaining > 0; }

    private:
        double sampleRate { 44100.0 };
        double transitionTotal { 0.0 };
        double transitionRemaining { 0.0 };
        double smoothingCoefficientPerSample { 0.0 };
        bool lastIndependent { false };
        ResolvedToneSpace current;
        ResolvedToneSpace transitionStart;
        double logHp { 0.0 };   // cutoff ramps in log2(Hz)
        double logLp { 0.0 };
    };
}
