#include "EnvelopeADSR.h"

#include "../Utility/ParameterIDs.h"

namespace texture
{
    EnvelopeADSR::EnvelopeADSR()
    {
        current.attack  = params::attackDefaultMs  * 0.001f;
        current.decay   = params::decayDefaultMs   * 0.001f;
        current.sustain = params::sustainDefault;
        current.release = params::releaseDefaultMs * 0.001f;

        adsr.setParameters (current);
    }

    void EnvelopeADSR::prepare (double sampleRate)
    {
        preparedSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        adsr.setSampleRate (preparedSampleRate);
        adsr.setParameters (current);
        adsr.reset();
        releasing = false;
    }

    void EnvelopeADSR::setParameters (float attackMs, float decayMs, float sustainLevel, float releaseMs)
    {
        const juce::ADSR::Parameters wanted {
            juce::jmax (0.001f, attackMs  * 0.001f),
            juce::jmax (0.001f, decayMs   * 0.001f),
            juce::jlimit (0.0f, 1.0f, sustainLevel),
            juce::jmax (0.001f, releaseMs * 0.001f)
        };

        // juce::ADSR recalculates its rates on every set, so only push real changes.
        if (juce::approximatelyEqual (wanted.attack,  current.attack)
            && juce::approximatelyEqual (wanted.decay,   current.decay)
            && juce::approximatelyEqual (wanted.sustain, current.sustain)
            && juce::approximatelyEqual (wanted.release, current.release))
            return;

        current = wanted;
        adsr.setParameters (current);
    }

    void EnvelopeADSR::noteOn()
    {
        releasing = false;
        adsr.noteOn();
    }

    void EnvelopeADSR::noteOff()
    {
        releasing = true;
        adsr.noteOff();
    }

    void EnvelopeADSR::reset()
    {
        releasing = false;
        adsr.reset();
    }
}
