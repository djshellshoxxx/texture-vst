#pragma once

#include "../Utility/ParameterIDs.h"

#include <cstdint>

namespace texture
{
    /** Where a layer reads its sample (F10). Stored as integers in host state and presets. */
    enum class ScanMode : std::uint8_t
    {
        legacy = 0,   // each voice's read head, as before
        manual,       // scan_position is the read centre
        scan          // the read centre moves at scan_speed and wraps
    };

    ScanMode scanModeFromIndex (int index) noexcept;

    /** The per-layer controls, as plain values. Out-of-range or non-finite values fall back to
        their defaults, never to an extreme. */
    struct ScanSettings
    {
        ScanMode mode { ScanMode::legacy };
        float position { params::scanPositionDefault };   // normalised read centre, [0, 1]
        float speed { params::scanSpeedDefault };         // source lengths per second, [-4, 4]
        bool freeze { params::freezeDefault };
        float freezeSpread { params::freezeSpreadDefault };   // jitter radius while frozen, [0, 1]
    };

    ScanSettings sanitiseScanSettings (const ScanSettings& settings) noexcept;

    /** What a layer does for one block. */
    struct ScanState
    {
        /** True: grains start at `centre`. False: the voices use their own read heads. */
        bool active { false };
        double centre { 0.0 };       // normalised, [0, 1]
        float jitter { 0.0f };       // grain start jitter radius, as a fraction of the source

        /** A freeze began in this block; the engine reports `latchedCentre` to the processor,
            which writes it into scan_position so the saved patch restores the same place. */
        bool latched { false };
        double latchedCentre { 0.0 };

        /** Freeze ended in Legacy mode: the voices must move their read heads to `centre`. */
        bool jumped { false };
    };

    /** Scan and freeze for one layer (F10). Pure: no JUCE audio objects, no allocation.

        Legacy with Freeze off returns inactive, so the default path is untouched. Manual
        glides the centre to scan_position over 20 ms. Scan advances it by speed and wraps.
        Freeze holds the centre where it was when Freeze turned on. Until the processor's
        write of scan_position arrives, the centre stays put, so a freeze never jumps. A
        later edit of scan_position moves a frozen centre over 20 ms. Jitter while frozen is
        freezeSpread x 0.25 of the source; otherwise it is the MORPH jitter. */
    class SamplePositionController
    {
    public:
        static constexpr double glideSeconds = 0.02;

        void prepare (double sampleRate) noexcept;

        /** Runtime reset, for a new sample: the centre returns to the saved position and any
            freeze is released, so a freeze still set starts again at the new sample. */
        void reset (float savedPosition) noexcept;

        /** Advances one block. `legacyCentre` is the voices' read head, normalised. */
        ScanState advance (const ScanSettings& settings, int numSamples, double sourceLength,
                           double legacyCentre, float morphJitter) noexcept;

        bool isFrozen() const noexcept { return frozen; }

    private:
        void setGoal (double goal) noexcept;
        void stepGlide (int numSamples) noexcept;

        double sampleRate { 44100.0 };
        int glideTotal { 882 };
        double centre { 0.0 };
        double goal { 0.0 };
        double glideStep { 0.0 };
        int glideRemaining { 0 };

        bool frozen { false };
        bool synced { false };
        double latchedCentre { 0.0 };
        float latchedPosition { 0.0f };

        bool hasLastPosition { false };
        float lastPosition { 0.0f };
    };
}
