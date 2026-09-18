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

        return parameterID;
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

        return 0.0f;
    }
}
