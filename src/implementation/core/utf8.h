#pragma once

#include "span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

inline size_t utf8SequenceLength(unsigned char lead)
{
    if (lead < 0x80) { return 1; }
    if (lead >= 0xC2 && lead <= 0xDF) { return 2; }
    if (lead >= 0xE0 && lead <= 0xEF) { return 3; }
    if (lead >= 0xF0 && lead <= 0xF4) { return 4; }
    return 0;
}

inline bool isUtf8Continuation(char byte)
{
    return (static_cast<unsigned char>(byte) & 0xC0u) == 0x80u;
}

inline uint32_t utf8CodePointAt(Span<const char> text, size_t index, size_t length)
{
    const auto lead = static_cast<unsigned char>(text[index]);
    if (length == 1) { return lead; }
    uint32_t point = static_cast<uint32_t>(lead) & (0xFFu >> (length + 1));
    for (size_t offset = 1; offset < length; ++offset) {
        point = (point << 6) | (static_cast<unsigned char>(text[index + offset]) & 0x3Fu);
    }
    return point;
}

inline bool isUtf8SequenceWellFormed(Span<const char> text, size_t index, size_t length)
{
    for (size_t offset = 1; offset < length; ++offset) {
        if (!isUtf8Continuation(text[index + offset])) { return false; }
    }
    const uint32_t point = utf8CodePointAt(text, index, length);
    if (length == 3 && (point < 0x800 || (point >= 0xD800 && point <= 0xDFFF))) { return false; }
    if (length == 4 && (point < 0x10000 || point > 0x10FFFF)) { return false; }
    return true;
}

inline bool isValidUtf8(Span<const char> text)
{
    size_t index = 0;
    while (index < text.length()) {
        const size_t length = utf8SequenceLength(static_cast<unsigned char>(text[index]));
        if (length == 0 || index + length > text.length()) { return false; }
        if (!isUtf8SequenceWellFormed(text, index, length)) { return false; }
        index += length;
    }
    return true;
}

}
