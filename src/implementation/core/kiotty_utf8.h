#pragma once

#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr unsigned char UTF8_SINGLE_BYTE_LIMIT = 0x80;
constexpr unsigned char UTF8_TWO_BYTE_LEAD_FIRST = 0xC2;
constexpr unsigned char UTF8_TWO_BYTE_LEAD_LAST = 0xDF;
constexpr unsigned char UTF8_THREE_BYTE_LEAD_FIRST = 0xE0;
constexpr unsigned char UTF8_THREE_BYTE_LEAD_LAST = 0xEF;
constexpr unsigned char UTF8_FOUR_BYTE_LEAD_FIRST = 0xF0;
constexpr unsigned char UTF8_FOUR_BYTE_LEAD_LAST = 0xF4;
constexpr size_t UTF8_TWO_BYTE_LENGTH = 2;
constexpr size_t UTF8_THREE_BYTE_LENGTH = 3;
constexpr size_t UTF8_FOUR_BYTE_LENGTH = 4;
constexpr uint32_t UTF8_CONTINUATION_MASK = 0xC0u;
constexpr uint32_t UTF8_CONTINUATION_TAG = 0x80u;
constexpr uint32_t UTF8_LEAD_PAYLOAD_MASK = 0xFFu;
constexpr uint32_t UTF8_CONTINUATION_PAYLOAD_BITS = 6;
constexpr uint32_t UTF8_CONTINUATION_PAYLOAD_MASK = 0x3Fu;
constexpr uint32_t UTF8_THREE_BYTE_MIN_POINT = 0x800;
constexpr uint32_t UTF8_SURROGATE_FIRST = 0xD800;
constexpr uint32_t UTF8_SURROGATE_LAST = 0xDFFF;
constexpr uint32_t UTF8_FOUR_BYTE_MIN_POINT = 0x10000;
constexpr uint32_t UTF8_MAX_POINT = 0x10FFFF;

inline size_t utf8SequenceLength(unsigned char lead);
inline bool isUtf8SequenceWellFormed(Span<const char> text, size_t index, size_t length);
inline bool isUtf8Continuation(char byte);
inline uint32_t utf8CodePointAt(Span<const char> text, size_t index, size_t length);

inline bool isValidUtf8(Span<const char> text)
{
    size_t index = 0;
    const size_t text_length = text.length();
    while (index < text_length) {
        const size_t length = utf8SequenceLength(static_cast<unsigned char>(text[index]));
        if (length == 0 || index + length > text_length) { return false; }
        const bool is_well_formed = isUtf8SequenceWellFormed(text, index, length);
        if (!is_well_formed) { return false; }
        index += length;
    }
    return true;
}

inline size_t utf8SequenceLength(unsigned char lead)
{
    if (lead < UTF8_SINGLE_BYTE_LIMIT) { return 1; }
    if (lead >= UTF8_TWO_BYTE_LEAD_FIRST && lead <= UTF8_TWO_BYTE_LEAD_LAST) {
        return UTF8_TWO_BYTE_LENGTH;
    }
    if (lead >= UTF8_THREE_BYTE_LEAD_FIRST && lead <= UTF8_THREE_BYTE_LEAD_LAST) {
        return UTF8_THREE_BYTE_LENGTH;
    }
    if (lead >= UTF8_FOUR_BYTE_LEAD_FIRST && lead <= UTF8_FOUR_BYTE_LEAD_LAST) {
        return UTF8_FOUR_BYTE_LENGTH;
    }
    return 0;
}

inline bool isUtf8SequenceWellFormed(Span<const char> text, size_t index, size_t length)
{
    for (size_t offset = 1; offset < length; ++offset) {
        const bool is_continuation = isUtf8Continuation(text[index + offset]);
        if (!is_continuation) { return false; }
    }
    const uint32_t point = utf8CodePointAt(text, index, length);
    const bool is_surrogate = point >= UTF8_SURROGATE_FIRST && point <= UTF8_SURROGATE_LAST;
    if (length == UTF8_THREE_BYTE_LENGTH && (point < UTF8_THREE_BYTE_MIN_POINT || is_surrogate)) {
        return false;
    }
    if (length == UTF8_FOUR_BYTE_LENGTH &&
        (point < UTF8_FOUR_BYTE_MIN_POINT || point > UTF8_MAX_POINT)) {
        return false;
    }
    return true;
}

inline bool isUtf8Continuation(char byte)
{
    return (static_cast<unsigned char>(byte) & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_TAG;
}

inline uint32_t utf8CodePointAt(Span<const char> text, size_t index, size_t length)
{
    const auto lead = static_cast<unsigned char>(text[index]);
    if (length == 1) { return lead; }
    uint32_t point = static_cast<uint32_t>(lead) & (UTF8_LEAD_PAYLOAD_MASK >> (length + 1));
    for (size_t offset = 1; offset < length; ++offset) {
        point = (point << UTF8_CONTINUATION_PAYLOAD_BITS) |
                (static_cast<unsigned char>(text[index + offset]) & UTF8_CONTINUATION_PAYLOAD_MASK);
    }
    return point;
}

}
