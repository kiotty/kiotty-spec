#pragma once

#include "kiotty_spec.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

// 10진수 글자 전체를 수로 바꾼다. 비었거나, 숫자가 아닌 글자가 있거나, maximum 을 넘으면 INVALID_ARGUMENT
inline Result<uint64_t, KiottySpecStatus> parseDecimal(Span<const char> text, uint64_t maximum)
{
    if (text.length() == 0) { return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    uint64_t number = 0;
    for (const char letter : text) {
        const bool digit = letter >= '0' && letter <= '9';
        const uint64_t value = static_cast<uint64_t>(letter - '0');
        if (!digit || number > (maximum - value) / 10) { return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        number = number * 10 + value;
    }
    return Result<uint64_t, KiottySpecStatus>::success(number);
}

// 정확히 digits 자리의 소문자 16진수 글자를 수로 바꾼다.
inline Result<uint64_t, KiottySpecStatus> parseHex(Span<const char> text, size_t digits)
{
    if (text.length() != digits || digits > 16) { return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
    uint64_t number = 0;
    for (const char letter : text) {
        const bool decimal = letter >= '0' && letter <= '9';
        const bool lower = letter >= 'a' && letter <= 'f';
        if (!decimal && !lower) { return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT); }
        const uint64_t value = decimal ? static_cast<uint64_t>(letter - '0') : static_cast<uint64_t>(letter - 'a' + 10);
        number = (number << 4) | value;
    }
    return Result<uint64_t, KiottySpecStatus>::success(number);
}

// number 의 아래 digits 자리를 소문자 16진수로 text 뒤에 붙인다. Text 는 append(Span<const char>) 를 가진다.
template <typename Text>
KiottySpecStatus appendHex(Text& text, uint64_t number, size_t digits)
{
    constexpr char LETTERS[] = "0123456789abcdef";
    constexpr size_t MAX_DIGITS = 16;
    char letters[MAX_DIGITS] {};
    const size_t count = digits < MAX_DIGITS ? digits : MAX_DIGITS;
    for (size_t index = 0; index < count; ++index) {
        const size_t shift = (count - 1 - index) * 4;
        letters[index] = LETTERS[(number >> shift) & 0xFu];
    }
    return text.append(Span<const char>(letters, count));
}

// 비트를 고르게 섞는다 (MurmurHash3 fmix64). FNV 는 끝 글자만 다른 입력의 상위 비트가 비슷해, 키처럼 정렬에 쓰는 값은 이것을 거친다.
inline uint64_t mix64(uint64_t value)
{
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33;
    return value;
}

// FNV-1a. 본문 해시와 항목 키를 만든다. 암호용이 아니다.
inline uint64_t hash64(Span<const char> bytes, uint64_t seed = 0xcbf29ce484222325ull)
{
    uint64_t hash = seed;
    for (const char letter : bytes) {
        hash ^= static_cast<uint8_t>(letter);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

}
