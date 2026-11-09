#pragma once

#include "kiotty_spec.h"
#include "kiotty_decimal.h"
#include "kiotty_span.h"
#include "kiotty_utf8.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>

// JSON 값을 글 뒤에 붙인다. Text 는 append(Span<const char>) 를 가진 TextBuffer · FixedString · SpanText 다.
// 출력은 예전에 쓰던 nlohmann::json 의 dump(-1, ' ', false, replace) 와 같아 jsonl 이 바뀌지 않는다. 잘못된 UTF-8 바이트는 U+FFFD 로 바꾼다.

namespace kiotty::spec::implementation {

template <typename Text>
KiottySpecStatus appendJsonString(Text& text, Span<const char> value)
{
    constexpr char HEX[] = "0123456789abcdef";
    constexpr char REPLACEMENT[] = "\xEF\xBF\xBD";
    KiottySpecStatus status = text.append(Span<const char>("\"", 1));
    size_t start = 0;
    size_t index = 0;
    auto flush = [&](size_t end) {
        if (status == KIOTTY_SPEC_STATUS_SUCCESS && end > start) { status = text.append(Span<const char>(value.data() + start, end - start)); }
    };
    while (index < value.length() && status == KIOTTY_SPEC_STATUS_SUCCESS) {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        const char* escape = nullptr;
        switch (byte) {
        case '"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape != nullptr) {
            flush(index);
            if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(Span<const char>(escape, 2)); }
            start = ++index;
            continue;
        }
        if (byte < 0x20) {
            flush(index);
            const char unicode[6] = {'\\', 'u', '0', '0', HEX[byte >> 4], HEX[byte & 0x0F]};
            if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(Span<const char>(unicode, 6)); }
            start = ++index;
            continue;
        }
        if (byte < 0x80) {
            ++index;
            continue;
        }
        const size_t length = utf8SequenceLength(byte);
        if (length > 1 && index + length <= value.length() && isUtf8SequenceWellFormed(value, index, length)) {
            index += length;
            continue;
        }
        flush(index);
        if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(Span<const char>(REPLACEMENT, 3)); }
        start = ++index;
    }
    flush(index);
    if (status == KIOTTY_SPEC_STATUS_SUCCESS) { status = text.append(Span<const char>("\"", 1)); }
    return status;
}

template <typename Text>
KiottySpecStatus appendJsonInteger(Text& text, int64_t value)
{
    if (value >= 0) { return appendDecimal(text, static_cast<uint64_t>(value)); }
    const KiottySpecStatus status = text.append(Span<const char>("-", 1));
    if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    // INT64_MIN 도 넘치지 않게 부호 없는 수로 바꾼다.
    return appendDecimal(text, uint64_t{0} - static_cast<uint64_t>(value));
}

// 가장 짧게 되돌아오는 표기. 정수 모양이면 ".0" 을 붙이고, 유한하지 않으면 null
template <typename Text>
KiottySpecStatus appendJsonReal(Text& text, double value)
{
    if (!std::isfinite(value)) { return text.append(Span<const char>("null", 4)); }
    char digits[32] {};
    const auto converted = std::to_chars(digits, digits + sizeof(digits), value);
    if (converted.ec != std::errc()) { return KIOTTY_SPEC_STATUS_CAPACITY_EXCEEDED; }
    const size_t length = static_cast<size_t>(converted.ptr - digits);
    KiottySpecStatus status = text.append(Span<const char>(digits, length));
    bool fractional = false;
    for (size_t index = 0; index < length; ++index) { fractional = fractional || digits[index] == '.' || digits[index] == 'e'; }
    if (status == KIOTTY_SPEC_STATUS_SUCCESS && !fractional) { status = text.append(Span<const char>(".0", 2)); }
    return status;
}

}
