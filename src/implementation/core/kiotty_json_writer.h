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

// value[index] 부터 이스케이프 없이 그대로 쓸 수 있는 바이트 수
inline size_t plainJsonRun(Span<const char> value, size_t index)
{
    const size_t start = index;
    while (index < value.length()) {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        const bool ascii = byte >= 0x20 && byte < 0x80 && byte != '"' && byte != '\\';
        const size_t length = ascii ? 1 : utf8SequenceLength(byte);
        const bool sequence = !ascii && length > 1 && index + length <= value.length() && isUtf8SequenceWellFormed(value, index, length);
        if (!ascii && !sequence) { break; }
        index += length;
    }
    return index - start;
}

// 그대로 쓸 수 없는 바이트 하나: 이스케이프, 제어 문자, 잘못된 UTF-8 바이트(U+FFFD)
template <typename Text>
KiottySpecStatus appendJsonSpecial(Text& text, unsigned char byte)
{
    constexpr char HEX[] = "0123456789abcdef";
    constexpr char REPLACEMENT[] = "\xEF\xBF\xBD";
    switch (byte) {
    case '"': return text.append(Span<const char>("\\\"", 2));
    case '\\': return text.append(Span<const char>("\\\\", 2));
    case '\b': return text.append(Span<const char>("\\b", 2));
    case '\f': return text.append(Span<const char>("\\f", 2));
    case '\n': return text.append(Span<const char>("\\n", 2));
    case '\r': return text.append(Span<const char>("\\r", 2));
    case '\t': return text.append(Span<const char>("\\t", 2));
    default: break;
    }
    if (byte >= 0x20) { return text.append(Span<const char>(REPLACEMENT, 3)); }
    const char unicode[6] = {'\\', 'u', '0', '0', HEX[byte >> 4], HEX[byte & 0x0F]};
    return text.append(Span<const char>(unicode, 6));
}

template <typename Text>
KiottySpecStatus appendJsonString(Text& text, Span<const char> value)
{
    KiottySpecStatus status = text.append(Span<const char>("\"", 1));
    size_t index = 0;
    while (index < value.length() && status == KIOTTY_SPEC_STATUS_SUCCESS) {
        const size_t run = plainJsonRun(value, index);
        status = text.append(Span<const char>(value.data() + index, run));
        index += run;
        if (index < value.length() && status == KIOTTY_SPEC_STATUS_SUCCESS) {
            status = appendJsonSpecial(text, static_cast<unsigned char>(value[index]));
            ++index;
        }
    }
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
