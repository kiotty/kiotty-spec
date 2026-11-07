#pragma once

#include "span.h"

#include <cstddef>
#include <string_view>

namespace kiotty::spec::implementation {

inline Span<const char> boundedText(const char* text, size_t limit)
{
    if (text == nullptr) { return Span<const char>(); }
    size_t length = 0;
    while (length < limit && text[length] != '\0') { ++length; }
    return Span<const char>(text, length);
}

template <size_t Length>
Span<const char> literalText(const char (&text)[Length])
{
    return Span<const char>(text, Length - 1);
}

inline Span<const char> spanOf(std::string_view text) { return Span<const char>(text.data(), text.size()); }

inline std::string_view viewOf(Span<const char> text)
{
    if (text.data() == nullptr) { return std::string_view(); }
    return std::string_view(text.data(), text.length());
}

}
