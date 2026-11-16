#pragma once

#include "../Utility/ParameterIDs.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace texture
{
    /** The five manual grain fields (F13). The order is the lock/UI order. */
    enum class GrainField : std::uint8_t
    {
        size = 0,
        density,
        jitter,
        detune,
        pan
    };

    inline constexpr int numGrainFields = 5;

    /** Manual grain values and their Follow flags. Plain values: the processor converts host
        parameters into this once per block, and the tests build it directly. */
    struct GrainControlSettings
    {
        /** Manual value per field, in its own units (ms, Hz, 0..1, cents, 0..1). */
        std::array<float, numGrainFields> values {
            params::grainSizeDefaultMs,
            params::grainDensityDefaultHz,
            params::grainJitterDefault,
            params::grainDetuneDefaultCents,
            params::grainPanSpreadDefault
        };

        /** True: the field follows the MORPH mapping and its manual value is ignored. */
        std::array<bool, numGrainFields> follow {
            params::grainFollowDefault, params::grainFollowDefault, params::grainFollowDefault,
            params::grainFollowDefault, params::grainFollowDefault
        };
    };

    /** Parameter IDs for each grain field, so the processor, the lock registry and the
        preset code use one mapping. */
    const char* grainValueParameterID (GrainField field) noexcept;
    const char* grainFollowParameterID (GrainField field) noexcept;

    /** Returns a copy whose values are finite and inside their ranges. A non-finite value
        falls back to its default, never to an extreme. */
    GrainControlSettings sanitiseGrainControls (const GrainControlSettings& settings) noexcept;

    /** What the voices render with for one block, and what the panel displays. */
    struct GrainResolution
    {
        /** MORPH targets with the grain fields replaced by their effective values. The
            non-grain fields are passed through untouched. */
        MorphTargets targets;

        /** Effective value per field: the manual value (smoothed) or the MORPH-derived one. */
        std::array<float, numGrainFields> effective {};

        /** Average grains sounding per voice that the settings ask for: duration x density. */
        float concurrency { 0.0f };

        /** True when concurrency exceeds the per-voice pool. Extra spawns are then skipped. */
        bool oversubscribed { false };
    };

    /** Applies the grain overrides once, to every consumer (F13).

        Follow MORPH on every field returns the MORPH targets exactly, so legacy patches
        render as before. A manual value takes effect over a 20 ms ramp for jitter, detune
        and pan spread; duration and density apply from the next block.

        Duration and density are the only grain-timing controls. With both manual, the
        overlap is derived from them. With only duration manual, the MORPH overlap is kept,
        so the legacy cadence scales with duration. With only density manual, the spawn
        interval is 1 / density and the overlap is derived from duration.

        Pure: no JUCE audio objects, no allocation. Call prepare() before advance(). */
    class GrainTargetResolver
    {
    public:
        static constexpr double rampSeconds = 0.02;

        void prepare (double sampleRate) noexcept;

        /** The next advance() snaps smoothed fields to their goal, with no ramp. */
        void reset() noexcept;

        /** Resolves one block of `numSamples`. Smoothed fields move toward their goal. */
        GrainResolution advance (const MorphTargets& morph,
                                 const GrainControlSettings& settings,
                                 int numSamples) noexcept;

        /** The mapping with no ramp. The goal values, for tests and the GUI. */
        static GrainResolution resolve (const MorphTargets& morph,
                                        const GrainControlSettings& settings) noexcept;

        /** Whether a grain field's manual value is what the next block will use. */
        static bool isManual (const GrainControlSettings& settings, GrainField field) noexcept
        {
            return ! settings.follow[static_cast<std::size_t> (field)];
        }

    private:
        struct Ramp
        {
            float value { 0.0f };
            float target { 0.0f };
            float step { 0.0f };
            int remaining { 0 };
            bool primed { false };
        };

        /** Moves one ramp by `numSamples` toward `goal`, starting from `followValue` while
            the field follows MORPH. Returns the value to render with. */
        float stepRamp (Ramp& ramp, float goal, float followValue, bool follows, int numSamples) noexcept;

        double sampleRate { 44100.0 };
        int rampSamples { 882 };
        std::array<Ramp, numGrainFields> ramps {};
    };

    /** Parameter writes that change one grain field's Follow state (F13). Engaging Follow
        writes only the flag. Disengaging first copies the effective value into the manual
        control, so the sound does not jump, then clears the flag. */
    struct GrainFollowChange
    {
        struct Write
        {
            const char* parameterID { nullptr };
            float value { 0.0f };
        };

        std::array<Write, 2> writes {};
        int count { 0 };
    };

    GrainFollowChange planGrainFollowChange (GrainField field, bool shouldFollow, float effectiveValue) noexcept;

    /** True when Gentle Mutate may change this grain parameter now. A manual value is
        mutable only while its Follow flag is off, because a value that Follow ignores
        would change nothing audible. Follow flags are never mutable. */
    bool isGrainParameterMutable (std::string_view parameterID, const GrainControlSettings& settings) noexcept;

    /** Concurrency at or below the pool plus this margin is not reported as oversubscribed,
        so the legacy MORPH maximum (8 grains) does not show a warning. */
    inline constexpr float oversubscriptionMargin = 0.5f;
}
