#pragma once

#include <juce_core/juce_core.h>

/** Central registry of every automatable parameter ID and its range.
    Nothing outside this header may spell a parameter ID as a literal.
*/
namespace texture::params
{
    // --- IDs -------------------------------------------------------------
    inline constexpr const char* morph       = "morph";
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* sustain     = "sustain";
    inline constexpr const char* release     = "release";
    inline constexpr const char* reverbOn    = "reverb_on";
    inline constexpr const char* gainDb      = "gain_db";
    inline constexpr const char* lfoRate     = "lfo_rate";
    inline constexpr const char* lfoDepth    = "lfo_depth";
    inline constexpr const char* lfoSync     = "lfo_sync";
    inline constexpr const char* lfoDivision = "lfo_division";
    inline constexpr const char* layerBlend  = "layer_blend";

    /** Every ID in preset/serialisation order. */
    inline constexpr const char* allIDs[] = {
        morph, attack, decay, sustain, release,
        reverbOn, gainDb, lfoRate, lfoDepth, lfoSync, lfoDivision, layerBlend
    };

    inline constexpr int numParameters = static_cast<int> (std::size (allIDs));

    // --- Ranges (08_MODULATION.md, 06_MORPH_KNOB_BEHAVIOUR.md, 07_EFFECTS_CHAIN.md) ---
    inline constexpr float morphMin = 0.0f,    morphMax = 1.0f,    morphDefault = 0.35f;
    inline constexpr float attackMinMs = 1.0f,   attackMaxMs = 5000.0f,  attackDefaultMs = 250.0f;
    inline constexpr float decayMinMs = 1.0f,    decayMaxMs = 5000.0f,   decayDefaultMs = 500.0f;
    inline constexpr float sustainMin = 0.0f,    sustainMax = 1.0f,      sustainDefault = 0.8f;
    inline constexpr float releaseMinMs = 10.0f, releaseMaxMs = 8000.0f, releaseDefaultMs = 1200.0f;
    inline constexpr bool  reverbOnDefault = true;
    inline constexpr float gainMinDb = -24.0f,   gainMaxDb = 6.0f,       gainDefaultDb = 0.0f;
    inline constexpr float lfoRateMinHz = 0.05f, lfoRateMaxHz = 8.0f,    lfoRateDefaultHz = 0.2f;
    inline constexpr float lfoDepthMin = 0.0f,   lfoDepthMax = 1.0f,     lfoDepthDefault = 0.0f;
    inline constexpr bool  lfoSyncDefault = false;
    inline constexpr int   lfoDivisionDefault = 2;   // index into divisionNames -> "1/4"
    inline constexpr float layerBlendMin = 0.0f, layerBlendMax = 1.0f,   layerBlendDefault = 0.0f;

    /** Tempo divisions offered when LFO sync is engaged. */
    inline constexpr const char* divisionNames[] = { "1/1", "1/2", "1/4", "1/8", "1/16" };
    inline constexpr int numDivisions = static_cast<int> (std::size (divisionNames));

    /** Beats per LFO cycle for each division index. */
    inline constexpr double divisionBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };

    /** Human-readable name shown in the host's automation list. */
    juce::String displayNameFor (const juce::String& parameterID);

    /** Default value for a parameter, in its own (real-world) units. */
    float defaultValueFor (const juce::String& parameterID);
}

/** The MORPH mapping table from 06_MORPH_KNOB_BEHAVIOUR.md.
    Every audible transformation in the plugin is derived here, once per block.
*/
namespace texture
{
    struct MorphTargets
    {
        int   grainsPerVoice;     // 1 .. 8
        float grainDurationMs;    // 20 .. 150
        float grainOverlap;       // 0.0 .. 0.875
        float positionJitter;     // 0.0 .. 0.3 (fraction of buffer length, quadratic)
        float detuneCents;        // 0 .. 30
        float panSpread;          // 0.0 .. 0.8
        float reverbSend;         // 0.15 .. 0.65
        float chorusDepth;        // 0.0 .. 0.6
        float saturatorDrive;     // 0.1 .. 0.35
        float highShelfTiltDb;    // 0.0 .. 2.0
    };

    /** Derives every MORPH-dependent value. `morph` must already be clamped to [0, 1]. */
    constexpr MorphTargets deriveMorphTargets (float morph) noexcept
    {
        const float m = morph < 0.0f ? 0.0f : (morph > 1.0f ? 1.0f : morph);

        MorphTargets t {};
        // linear round: 1 grain at 0, 8 grains at 1
        t.grainsPerVoice    = 1 + static_cast<int> (m * 7.0f + 0.5f);
        t.grainDurationMs   = 20.0f + m * 130.0f;
        t.grainOverlap      = m * 0.875f;
        t.positionJitter    = m * m * 0.3f;          // quadratic curve
        t.detuneCents       = m * 30.0f;
        t.panSpread         = m * 0.8f;
        t.reverbSend        = 0.15f + m * 0.5f;
        t.chorusDepth       = m * 0.6f;
        t.saturatorDrive    = 0.1f + m * 0.25f;
        t.highShelfTiltDb   = m * 2.0f;
        return t;
    }
}
