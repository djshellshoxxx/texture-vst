#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <optional>

namespace texture
{
    class ControlValueClipboard final
    {
    public:
        static void copy (const juce::String& parameterID, double value)
        {
            copiedParameterID() = parameterID;
            copiedValue() = value;
            hasValue() = true;
        }

        static void clear()
        {
            copiedParameterID().clear();
            copiedValue() = 0.0;
            hasValue() = false;
        }

        static bool canPaste (const juce::String& parameterID,
                              double minimum,
                              double maximum)
        {
            return valueFor (parameterID, minimum, maximum).has_value();
        }

        static std::optional<double> valueFor (const juce::String& parameterID,
                                               double minimum,
                                               double maximum)
        {
            if (! hasValue() || copiedParameterID() != parameterID
                || minimum > maximum || ! std::isfinite (copiedValue())
                || copiedValue() < minimum || copiedValue() > maximum)
            {
                return std::nullopt;
            }

            return copiedValue();
        }

    private:
        static juce::String& copiedParameterID()
        {
            static juce::String id;
            return id;
        }

        static double& copiedValue()
        {
            static double value = 0.0;
            return value;
        }

        static bool& hasValue()
        {
            static bool valuePresent = false;
            return valuePresent;
        }
    };
}
