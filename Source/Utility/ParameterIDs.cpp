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

        return 0.0f;
    }
}
