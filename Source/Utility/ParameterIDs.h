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
    inline constexpr const char* tune        = "tune";
    inline constexpr const char* layer1Root = "layer_1_root";
    inline constexpr const char* layer2Root = "layer_2_root";
    inline constexpr const char* layer1FineCents = "layer_1_fine_cents";
    inline constexpr const char* layer2FineCents = "layer_2_fine_cents";
    inline constexpr const char* reverse     = "reverse";
    inline constexpr const char* shimmer     = "shimmer";
    inline constexpr const char* randomMode  = "random_mode";

    // F07 independent tone and space. Appended, so older presets and sessions do not
    // carry them and read the Follow MORPH defaults.
    inline constexpr const char* toneSpaceMode = "tone_space_mode";
    inline constexpr const char* toneHpHz      = "tone_hp_hz";
    inline constexpr const char* toneLpHz      = "tone_lp_hz";
    inline constexpr const char* toneFilterOn  = "tone_filter_on";
    inline constexpr const char* toneTiltDb    = "tone_tilt_db";
    inline constexpr const char* spaceWidth    = "space_width";
    inline constexpr const char* spaceReverb   = "space_reverb";
    inline constexpr const char* spaceChorus   = "space_chorus";
    inline constexpr const char* toneDrive     = "tone_drive";

    // F11 mono, legato and glide. Appended; Poly with zero glide is the default.
    inline constexpr const char* voiceMode     = "voice_mode";
    inline constexpr const char* notePriority  = "note_priority";
    inline constexpr const char* glideMs       = "glide_ms";
    inline constexpr const char* glideMode     = "glide_mode";

    // F13 expandable grain controls. Appended. Each manual value has a Follow flag;
    // while Follow is on the value is ignored and the MORPH mapping applies.
    inline constexpr const char* grainSizeMs       = "grain_size_ms";
    inline constexpr const char* grainSizeFollow   = "grain_size_follow";
    inline constexpr const char* grainDensityHz    = "grain_density_hz";
    inline constexpr const char* grainDensityFollow = "grain_density_follow";
    inline constexpr const char* grainJitter       = "grain_jitter";
    inline constexpr const char* grainJitterFollow = "grain_jitter_follow";
    inline constexpr const char* grainDetuneCents  = "grain_detune_cents";
    inline constexpr const char* grainDetuneFollow = "grain_detune_follow";
    inline constexpr const char* grainPanSpread    = "grain_pan_spread";
    inline constexpr const char* grainPanFollow    = "grain_pan_follow";

    // F14 modulation shapes. Appended. lfo_rate, depth, sync and division are unchanged.
    inline constexpr const char* lfoShape      = "lfo_shape";
    inline constexpr const char* lfoRetrigger  = "lfo_retrigger";
    inline constexpr const char* lfoPhase      = "lfo_phase";
    inline constexpr const char* lfoSlewMs     = "lfo_slew_ms";

    // F19 rhythmic grains. Appended. Legacy clock is the default and leaves the grain path unchanged.
    inline constexpr const char* grainClock          = "grain_clock";
    inline constexpr const char* grainDivision       = "grain_division";
    inline constexpr const char* grainSwing          = "grain_swing";
    inline constexpr const char* grainProbability    = "grain_probability";
    inline constexpr const char* grainGate           = "grain_gate";
    inline constexpr const char* grainBurst          = "grain_burst";
    inline constexpr const char* grainFirstImmediate = "grain_first_immediate";

    // F10 freeze and scan. Appended, one set per layer.
    inline constexpr const char* layer1ScanMode     = "layer_1_scan_mode";
    inline constexpr const char* layer1ScanPosition = "layer_1_scan_position";
    inline constexpr const char* layer1ScanSpeed    = "layer_1_scan_speed";
    inline constexpr const char* layer1Freeze       = "layer_1_freeze";
    inline constexpr const char* layer1FreezeSpread = "layer_1_freeze_spread";
    inline constexpr const char* layer2ScanMode     = "layer_2_scan_mode";
    inline constexpr const char* layer2ScanPosition = "layer_2_scan_position";
    inline constexpr const char* layer2ScanSpeed    = "layer_2_scan_speed";
    inline constexpr const char* layer2Freeze       = "layer_2_freeze";
    inline constexpr const char* layer2FreezeSpread = "layer_2_freeze_spread";

    /** Every ID in preset/serialisation order. Append only: positions are part of
        the lock registry's contract and of the preset file layout. */
    inline constexpr const char* allIDs[] = {
        morph, attack, decay, sustain, release,
        reverbOn, gainDb, lfoRate, lfoDepth, lfoSync, lfoDivision, layerBlend,
        tune, reverse, shimmer, randomMode,
        layer1Root, layer2Root, layer1FineCents, layer2FineCents,
        toneSpaceMode, toneHpHz, toneLpHz, toneFilterOn, toneTiltDb,
        spaceWidth, spaceReverb, spaceChorus, toneDrive,
        voiceMode, notePriority, glideMs, glideMode,
        grainSizeMs, grainSizeFollow, grainDensityHz, grainDensityFollow,
        grainJitter, grainJitterFollow, grainDetuneCents, grainDetuneFollow,
        grainPanSpread, grainPanFollow,
        lfoShape, lfoRetrigger, lfoPhase, lfoSlewMs,
        layer1ScanMode, layer1ScanPosition, layer1ScanSpeed, layer1Freeze, layer1FreezeSpread,
        layer2ScanMode, layer2ScanPosition, layer2ScanSpeed, layer2Freeze, layer2FreezeSpread,
        grainClock, grainDivision, grainSwing, grainProbability, grainGate, grainBurst, grainFirstImmediate
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
    // v0.0.1 additions. All default to "off" so older presets and sessions,
    // which do not carry them, sound exactly as they did.
    inline constexpr float tuneMinSemitones = -24.0f, tuneMaxSemitones = 24.0f, tuneDefaultSemitones = 0.0f;
    inline constexpr float sourceRootMinMidi = 0.0f, sourceRootMaxMidi = 127.0f, sourceRootDefaultMidi = 60.0f;
    inline constexpr float sourceFineMinCents = -100.0f, sourceFineMaxCents = 100.0f, sourceFineDefaultCents = 0.0f;
    inline constexpr int randomModeDefault = 0; // Legacy preserves the original random stream.
    inline constexpr float reverseMin = 0.0f,    reverseMax = 1.0f,      reverseDefault = 0.0f;
    inline constexpr float shimmerMin = 0.0f,    shimmerMax = 1.0f,      shimmerDefault = 0.0f;

    // F07 ranges. Follow MORPH (mode 0) is the default and ignores these values.
    inline constexpr int toneSpaceModeFollow = 0;
    inline constexpr int toneSpaceModeIndependent = 1;
    inline constexpr int toneSpaceModeDefault = toneSpaceModeFollow;
    inline constexpr float toneHpMinHz = 20.0f,     toneHpMaxHz = 2000.0f, toneHpDefaultHz = 20.0f;
    inline constexpr float toneLpMinHz = 1000.0f,   toneLpMaxHz = 20000.0f, toneLpDefaultHz = 20000.0f;
    inline constexpr bool  toneFilterOnDefault = false;
    inline constexpr float toneTiltMinDb = -12.0f,  toneTiltMaxDb = 12.0f, toneTiltDefaultDb = 0.0f;
    inline constexpr float spaceWidthMin = 0.0f,    spaceWidthMax = 2.0f,  spaceWidthDefault = 1.0f;
    inline constexpr float spaceReverbMin = 0.0f,   spaceReverbMax = 1.0f, spaceReverbDefault = 0.3f;
    inline constexpr float spaceChorusMin = 0.0f,   spaceChorusMax = 1.0f, spaceChorusDefault = 0.2f;
    inline constexpr float toneDriveMin = 0.0f,     toneDriveMax = 1.0f,   toneDriveDefault = 0.1f;

    // F11 ranges. Poly (0) is the default; glide is off until the user asks for it.
    inline constexpr int voiceModeCount = 3;          // Poly, Mono retrigger, Mono legato
    inline constexpr int voiceModeDefault = 0;
    inline constexpr int notePriorityCount = 3;       // Last, High, Low
    inline constexpr int notePriorityDefault = 0;
    inline constexpr float glideMsMin = 0.0f,       glideMsMax = 2000.0f, glideMsDefault = 0.0f;
    inline constexpr int glideModeCount = 2;          // Always, Overlap only
    inline constexpr int glideModeDefault = 1;

    // F13 ranges. Follow is on by default, which reproduces the MORPH mapping exactly.
    inline constexpr float grainSizeMinMs = 5.0f,       grainSizeMaxMs = 500.0f,  grainSizeDefaultMs = 80.0f;
    inline constexpr float grainDensityMinHz = 1.0f,    grainDensityMaxHz = 200.0f, grainDensityDefaultHz = 30.0f;
    inline constexpr float grainJitterMin = 0.0f,       grainJitterMax = 1.0f,    grainJitterDefault = 0.1f;
    inline constexpr float grainDetuneMinCents = 0.0f,  grainDetuneMaxCents = 100.0f, grainDetuneDefaultCents = 10.0f;
    inline constexpr float grainPanSpreadMin = 0.0f,    grainPanSpreadMax = 1.0f, grainPanSpreadDefault = 0.5f;
    inline constexpr bool  grainFollowDefault = true;

    // F14 ranges. Sine with Legacy retrigger and zero phase is the default, which reproduces
    // the original MORPH LFO exactly.
    inline constexpr int lfoShapeCount = 5;            // Sine, Triangle, Step random, Smooth random, Curve
    inline constexpr int lfoShapeDefault = 0;
    inline constexpr int lfoRetriggerCount = 4;        // Legacy, Free, First note, Host
    inline constexpr int lfoRetriggerDefault = 0;
    inline constexpr float lfoPhaseMin = 0.0f,         lfoPhaseMax = 1.0f,   lfoPhaseDefault = 0.0f;
    inline constexpr float lfoSlewMsMin = 0.0f,        lfoSlewMsMax = 100.0f, lfoSlewMsDefault = 10.0f;

    // F10 ranges. Legacy (0) with Freeze off is the default, so the read position is unchanged.
    inline constexpr int scanModeCount = 3;            // Legacy, Manual, Scan
    inline constexpr int scanModeDefault = 0;
    inline constexpr float scanPositionMin = 0.0f,     scanPositionMax = 1.0f, scanPositionDefault = 0.0f;
    inline constexpr float scanSpeedMin = -4.0f,       scanSpeedMax = 4.0f,    scanSpeedDefault = 0.0f;
    inline constexpr bool  freezeDefault = false;
    inline constexpr float freezeSpreadMin = 0.0f,     freezeSpreadMax = 1.0f, freezeSpreadDefault = 0.1f;

    // F19 ranges. Legacy (0) is the default; the grid settings only matter when Rhythmic is chosen.
    inline constexpr int grainClockCount = 2;          // Legacy, Rhythmic
    inline constexpr int grainClockDefault = 0;
    inline constexpr int grainDivisionCount = 6;       // 1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T
    inline constexpr int grainDivisionDefault = 2;     // 1/16
    inline constexpr float grainSwingMin = 0.0f,       grainSwingMax = 0.75f, grainSwingDefault = 0.0f;
    inline constexpr float grainProbabilityMin = 0.0f, grainProbabilityMax = 1.0f, grainProbabilityDefault = 1.0f;
    inline constexpr float grainGateMin = 0.05f,       grainGateMax = 1.0f,   grainGateDefault = 0.5f;
    inline constexpr int grainBurstMin = 1,            grainBurstMax = 4,     grainBurstDefault = 1;
    inline constexpr bool grainFirstImmediateDefault = true;

    /** Grid labels for grain_division, in choice order. */
    inline constexpr const char* grainDivisionNames[] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };

    /** Tempo divisions offered when LFO sync is engaged. */
    inline constexpr const char* divisionNames[] = { "1/1", "1/2", "1/4", "1/8", "1/16" };
    inline constexpr int numDivisions = static_cast<int> (std::size (divisionNames));

    /** Beats per LFO cycle for each division index. */
    inline constexpr double divisionBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };

    /** True for the F13 grain value and Follow controls. */
    bool isGrainParameter (const juce::String& parameterID);

    /** True for the F14 modulation shape controls. */
    bool isModulationShapeParameter (const juce::String& parameterID);

    /** True for the F19 rhythmic grain controls. Randomize leaves them as the user set them. */
    bool isRhythmParameter (const juce::String& parameterID);

    /** True for the F10 scan and freeze controls. Randomize leaves them as the user set them. */
    bool isScanParameter (const juce::String& parameterID);

    /** Human-readable name shown in the host's automation list. */
    juce::String displayNameFor (const juce::String& parameterID);

    /** True for the F07 tone and space controls. Randomize leaves them as the user set them. */
    bool isToneSpaceParameter (const juce::String& parameterID);

    /** True for the F11 voice and glide controls. Randomize leaves them as the user set them. */
    bool isPerformanceParameter (const juce::String& parameterID);

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

        // Not driven by MORPH: copied in from their own parameters each block so
        // they travel to every grain through the same path as the MORPH values.
        float tuneSemitones;      // -24 .. +24, retunes samples not recorded at C3
        float sourceFineCents { 0.0f }; // smoothed, per-layer calibration
        float reverseChance;      // 0 .. 1, probability a grain plays backwards
        float shimmerAmount;      // 0 .. 1, octave-up grains blended into the cloud

        // F10: a manual read centre for the layer, normalised, or -1 when the layer reads from
        // its own read head (Legacy, not frozen). Set by SamplePositionController via the engine.
        float scanCentre { -1.0f };
        // F13: spawn rate in grains per second per voice, set only when grain density is
        // manual. Zero keeps the legacy cadence, which derives the hop from duration and
        // overlap. Written by GrainTargetResolver, never by MORPH.
        float grainDensityHz { 0.0f };
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
        t.tuneSemitones     = 0.0f;
        t.sourceFineCents   = 0.0f;
        t.reverseChance     = 0.0f;
        t.shimmerAmount     = 0.0f;
        t.grainDensityHz    = 0.0f;
        t.scanCentre        = -1.0f;
        return t;
    }
}
