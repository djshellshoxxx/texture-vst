#include "ParameterIDs.h"

namespace texture::params
{
    juce::String displayNameFor (const juce::String& parameterID)
    {
        if (parameterID == morph)       return "Morph";
        if (parameterID == attack)      return "Attack";
        if (parameterID == decay)       return "Decay";
        if (parameterID == sustain)     return "Sustain";
        if (parameterID == release)     return "Release";
        if (parameterID == reverbOn)    return "Reverb";
        if (parameterID == gainDb)      return "Output Gain";
        if (parameterID == lfoRate)     return "LFO Rate";
        if (parameterID == lfoDepth)    return "LFO Depth";
        if (parameterID == lfoSync)     return "LFO Sync";
        if (parameterID == lfoDivision) return "LFO Division";
        if (parameterID == layerBlend)  return "Layer Blend";
        if (parameterID == tune)        return "Tune";
        if (parameterID == layer1Root)  return "Layer 1 Source Note";
        if (parameterID == layer2Root)  return "Layer 2 Source Note";
        if (parameterID == layer1FineCents) return "Layer 1 Fine Tune";
        if (parameterID == layer2FineCents) return "Layer 2 Fine Tune";
        if (parameterID == reverse)     return "Reverse";
        if (parameterID == shimmer)     return "Shimmer";
        if (parameterID == randomMode)  return "Random Mode";
        if (parameterID == toneSpaceMode) return "Tone/Space Mode";
        if (parameterID == toneHpHz)    return "Tone High-Pass";
        if (parameterID == toneLpHz)    return "Tone Low-Pass";
        if (parameterID == toneFilterOn) return "Tone Filter";
        if (parameterID == toneTiltDb)  return "Tone Tilt";
        if (parameterID == spaceWidth)  return "Space Width";
        if (parameterID == spaceReverb) return "Space Reverb";
        if (parameterID == spaceChorus) return "Space Chorus";
        if (parameterID == toneDrive)   return "Tone Drive";
        if (parameterID == voiceMode)   return "Voice Mode";
        if (parameterID == notePriority) return "Note Priority";
        if (parameterID == glideMs)     return "Glide";
        if (parameterID == glideMode)   return "Glide Mode";
        if (parameterID == grainSizeMs) return "Grain Size";
        if (parameterID == grainSizeFollow) return "Grain Size Follow MORPH";
        if (parameterID == grainDensityHz) return "Grain Density";
        if (parameterID == grainDensityFollow) return "Grain Density Follow MORPH";
        if (parameterID == grainJitter) return "Grain Jitter";
        if (parameterID == grainJitterFollow) return "Grain Jitter Follow MORPH";
        if (parameterID == grainDetuneCents) return "Grain Detune";
        if (parameterID == grainDetuneFollow) return "Grain Detune Follow MORPH";
        if (parameterID == grainPanSpread) return "Grain Pan Spread";
        if (parameterID == grainPanFollow) return "Grain Pan Follow MORPH";
        if (parameterID == lfoShape)     return "LFO Shape";
        if (parameterID == lfoRetrigger) return "LFO Retrigger";
        if (parameterID == lfoPhase)     return "LFO Phase";
        if (parameterID == lfoSlewMs)    return "LFO Slew";

        return parameterID;
    }

    bool isToneSpaceParameter (const juce::String& parameterID)
    {
        for (const auto* id : { toneSpaceMode, toneHpHz, toneLpHz, toneFilterOn, toneTiltDb,
                                spaceWidth, spaceReverb, spaceChorus, toneDrive })
            if (parameterID == id)
                return true;

        return false;
    }

    bool isPerformanceParameter (const juce::String& parameterID)
    {
        for (const auto* id : { voiceMode, notePriority, glideMs, glideMode })
            if (parameterID == id)
                return true;

        return false;
    }

    bool isGrainParameter (const juce::String& parameterID)
    {
        for (const auto* id : { grainSizeMs, grainSizeFollow, grainDensityHz, grainDensityFollow,
                                grainJitter, grainJitterFollow, grainDetuneCents, grainDetuneFollow,
                                grainPanSpread, grainPanFollow })
            if (parameterID == id)
                return true;

        return false;
    }

    bool isModulationShapeParameter (const juce::String& parameterID)
    {
        for (const auto* id : { lfoShape, lfoRetrigger, lfoPhase, lfoSlewMs })
            if (parameterID == id)
                return true;

        return false;
    }

    float defaultValueFor (const juce::String& parameterID)
    {
        if (parameterID == morph)       return morphDefault;
        if (parameterID == attack)      return attackDefaultMs;
        if (parameterID == decay)       return decayDefaultMs;
        if (parameterID == sustain)     return sustainDefault;
        if (parameterID == release)     return releaseDefaultMs;
        if (parameterID == reverbOn)    return reverbOnDefault ? 1.0f : 0.0f;
        if (parameterID == gainDb)      return gainDefaultDb;
        if (parameterID == lfoRate)     return lfoRateDefaultHz;
        if (parameterID == lfoDepth)    return lfoDepthDefault;
        if (parameterID == lfoSync)     return lfoSyncDefault ? 1.0f : 0.0f;
        if (parameterID == lfoDivision) return static_cast<float> (lfoDivisionDefault);
        if (parameterID == layerBlend)  return layerBlendDefault;
        if (parameterID == tune)        return tuneDefaultSemitones;
        if (parameterID == layer1Root || parameterID == layer2Root) return sourceRootDefaultMidi;
        if (parameterID == layer1FineCents || parameterID == layer2FineCents) return sourceFineDefaultCents;
        if (parameterID == reverse)     return reverseDefault;
        if (parameterID == shimmer)     return shimmerDefault;
        if (parameterID == randomMode)  return randomModeDefault;
        if (parameterID == toneSpaceMode) return static_cast<float> (toneSpaceModeDefault);
        if (parameterID == toneHpHz)    return toneHpDefaultHz;
        if (parameterID == toneLpHz)    return toneLpDefaultHz;
        if (parameterID == toneFilterOn) return toneFilterOnDefault ? 1.0f : 0.0f;
        if (parameterID == toneTiltDb)  return toneTiltDefaultDb;
        if (parameterID == spaceWidth)  return spaceWidthDefault;
        if (parameterID == spaceReverb) return spaceReverbDefault;
        if (parameterID == spaceChorus) return spaceChorusDefault;
        if (parameterID == toneDrive)   return toneDriveDefault;
        if (parameterID == voiceMode)   return static_cast<float> (voiceModeDefault);
        if (parameterID == notePriority) return static_cast<float> (notePriorityDefault);
        if (parameterID == glideMs)     return glideMsDefault;
        if (parameterID == glideMode)   return static_cast<float> (glideModeDefault);
        if (parameterID == grainSizeMs) return grainSizeDefaultMs;
        if (parameterID == grainSizeFollow) return grainFollowDefault ? 1.0f : 0.0f;
        if (parameterID == grainDensityHz) return grainDensityDefaultHz;
        if (parameterID == grainDensityFollow) return grainFollowDefault ? 1.0f : 0.0f;
        if (parameterID == grainJitter) return grainJitterDefault;
        if (parameterID == grainJitterFollow) return grainFollowDefault ? 1.0f : 0.0f;
        if (parameterID == grainDetuneCents) return grainDetuneDefaultCents;
        if (parameterID == grainDetuneFollow) return grainFollowDefault ? 1.0f : 0.0f;
        if (parameterID == grainPanSpread) return grainPanSpreadDefault;
        if (parameterID == grainPanFollow) return grainFollowDefault ? 1.0f : 0.0f;
        if (parameterID == lfoShape)     return static_cast<float> (lfoShapeDefault);
        if (parameterID == lfoRetrigger) return static_cast<float> (lfoRetriggerDefault);
        if (parameterID == lfoPhase)     return lfoPhaseDefault;
        if (parameterID == lfoSlewMs)    return lfoSlewMsDefault;

        return 0.0f;
    }
}
