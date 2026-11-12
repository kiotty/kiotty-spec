#pragma once

#include "kiotty_spec.h"
#include "kiotty_fixed_string.h"
#include "kiotty_result.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr uint64_t FNV_OFFSET_BASIS = 0xcbf29ce484222325ull;
constexpr uint64_t FNV_PRIME = 0x100000001b3ull;
constexpr size_t MAX_HEX_DIGITS = 16;

using HexText = FixedString<MAX_HEX_DIGITS>;

inline Result<uint64_t, KiottySpecStatus> parseDecimal(Span<const char> text, uint64_t maximum)
{
    const size_t length = text.length();
    if (length == 0) {
        return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    constexpr uint64_t DECIMAL_BASE = 10;
    uint64_t number = 0;
    for (const char letter : text) {
        const bool is_digit = letter >= '0' && letter <= '9';
        const uint64_t value = static_cast<uint64_t>(letter - '0');
        const bool is_accepted = is_digit && number <= (maximum - value) / DECIMAL_BASE;
        if (!is_accepted) {
            return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
        }
        number = number * DECIMAL_BASE + value;
    }
    return Result<uint64_t, KiottySpecStatus>::success(number);
}

inline Result<uint64_t, KiottySpecStatus> parseHex(Span<const char> text, size_t digits)
{
    constexpr size_t MAX_DIGITS = 16;
    constexpr int FIRST_LETTER_VALUE = 10;
    constexpr uint32_t DIGIT_BITS = 4;
    const size_t length = text.length();
    if (length != digits || digits > MAX_DIGITS) {
        return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
    }
    uint64_t number = 0;
    for (const char letter : text) {
        const bool is_decimal = letter >= '0' && letter <= '9';
        const bool is_lower_hex = letter >= 'a' && letter <= 'f';
        const bool is_hex_digit = is_decimal || is_lower_hex;
        if (!is_hex_digit) {
            return Result<uint64_t, KiottySpecStatus>::failure(KIOTTY_SPEC_STATUS_INVALID_ARGUMENT);
        }
        const uint64_t value =
            is_decimal ? static_cast<uint64_t>(letter - '0')
                       : static_cast<uint64_t>(letter - 'a' + FIRST_LETTER_VALUE);
        number = (number << DIGIT_BITS) | value;
    }
    return Result<uint64_t, KiottySpecStatus>::success(number);
}

inline HexText formatHex(uint64_t number, size_t digits)
{
    constexpr char LETTERS[] = "0123456789abcdef";
    constexpr uint32_t DIGIT_BITS = 4;
    constexpr uint64_t DIGIT_MASK = 0xFu;
    char letters[MAX_HEX_DIGITS] {};
    const size_t count = digits < MAX_HEX_DIGITS ? digits : MAX_HEX_DIGITS;
    uint64_t rest = number;
    for (size_t index = count; index > 0; --index) {
        letters[index - 1] = LETTERS[rest & DIGIT_MASK];
        rest >>= DIGIT_BITS;
    }
    HexText text {};
    text.assign(Span<const char>(letters, count));
    return text;
}

inline uint64_t mix64(uint64_t value)
{
    constexpr uint32_t MIX_SHIFT = 33;
    constexpr uint64_t FIRST_MIX_MULTIPLIER = 0xff51afd7ed558ccdull;
    constexpr uint64_t SECOND_MIX_MULTIPLIER = 0xc4ceb9fe1a85ec53ull;
    value ^= value >> MIX_SHIFT;
    value *= FIRST_MIX_MULTIPLIER;
    value ^= value >> MIX_SHIFT;
    value *= SECOND_MIX_MULTIPLIER;
    value ^= value >> MIX_SHIFT;
    return value;
}

inline uint64_t hash64(Span<const char> bytes, uint64_t seed = FNV_OFFSET_BASIS)
{
    uint64_t hash = seed;
    for (const char letter : bytes) {
        hash ^= static_cast<uint8_t>(letter);
        hash *= FNV_PRIME;
    }
    return hash;
}

}
