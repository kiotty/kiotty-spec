#pragma once

#include "span.h"

#include <cstddef>

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

}
