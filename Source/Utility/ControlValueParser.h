#pragma once

#include <juce_core/juce_core.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace texture
{
    class ControlValueParser final
    {
    public:
        static std::optional<double> parse (const juce::String& text,
                                            double minimum,
                                            double maximum) noexcept
        {
            const auto trimmed = text.trim();

            if (trimmed.isEmpty() || minimum > maximum)
                return std::nullopt;

            const auto* begin = trimmed.toRawUTF8();
            char* end = nullptr;

            errno = 0;
            const auto value = std::strtod (begin, &end);

            if (end == begin || end == nullptr || *end != '\0' || errno == ERANGE
                || ! std::isfinite (value) || value < minimum || value > maximum)
            {
                return std::nullopt;
            }

            return value;
        }
    };
}
