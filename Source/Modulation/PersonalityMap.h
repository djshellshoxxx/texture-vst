#pragma once

#include "../Utility/ParameterIDs.h"

#include <string_view>

namespace texture
{
    /** MORPH personalities (F16). Legacy is the original mapping and is the default. */
    enum class Personality : int
    {
        legacy = 0,
        cloud,
        pulse,
        fracture,
        swarm
    };

    namespace personality
    {
        /** Discrete MORPH regions Pulse uses for its grid: 1/4, 1/8, 1/16 and 1/32. */
        inline constexpr int pulseRegionCount = 4;

        /** Style changes crossfade continuous targets and hold scheduler selection for this long. */
        inline constexpr double transitionSeconds = 0.02;

        /** Maps a stored index to a personality. Out-of-range values are Legacy. */
        Personality fromIndex (int index) noexcept;
    }

    /** The grain-level targets one personality asks for at one MORPH position, plus the
        scheduler intent Pulse adds. Absolute values, already bounded. `identity` means the
        legacy targets are used unchanged, so the default path does no arithmetic at all. */
    struct PersonalityTargets
    {
        bool identity { true };
        float grainDurationMs { 0.0f };
        float positionJitter { 0.0f };
        float detuneCents { 0.0f };
        float panSpread { 0.0f };
        float reverseOffset { 0.0f };    // added to the user's reverse chance, then clamped

        bool pulse { false };            // Pulse engages the grid at intensity > 0
        int pulseDivision { 2 };         // index into grainDivisionNames
        float pulseGate { 0.5f };        // the user's gate, moved toward the Pulse gate by intensity
    };

    class PersonalityMap
    {
    public:
        /** Targets for one personality. `legacy` holds the MORPH-derived values that intensity
            blends toward. Legacy, or intensity zero, returns the identity. Pure. */
        static PersonalityTargets map (float morph, Personality style, float intensity,
                                       const MorphTargets& legacy, float userGate) noexcept;

        /** Writes the mapped values into `targets`. The identity leaves them untouched. */
        static void apply (MorphTargets& targets, const PersonalityTargets& mapped) noexcept;

        /** Linear blend of two mappings. Discrete scheduler fields are taken from `b`. */
        static PersonalityTargets blend (const PersonalityTargets& a, const PersonalityTargets& b, float t) noexcept;
    };

    /** Holds the previous style for 20 ms after a change, so continuous targets crossfade and
        the scheduler keeps its previous choice until the crossfade completes. Audio thread. */
    class PersonalityRamp
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        /** The mapping for this block. A style change starts a 20 ms crossfade. */
        PersonalityTargets advance (float morph, Personality style, float intensity,
                                    const MorphTargets& legacy, float userGate, int numSamples) noexcept;

        bool isCrossfading() const noexcept { return rampRemaining > 0; }

    private:
        Personality current { Personality::legacy };
        Personality previous { Personality::legacy };
        int rampTotal { 882 };
        int rampRemaining { 0 };
        bool primed { false };
    };

    /** Whether Gentle Mutate may move a personality control now. The style choice is never
        mutated. The intensity moves only while a style other than Legacy is selected. */
    bool isPersonalityParameterMutable (std::string_view parameterID, int style) noexcept;
}
