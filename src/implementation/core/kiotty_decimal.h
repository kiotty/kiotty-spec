#pragma once

#include "kiotty_spec.h"
#include "kiotty_fixed_string.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

constexpr size_t MAX_DECIMAL_DIGITS = 20;

using DecimalText = FixedString<MAX_DECIMAL_DIGITS>;

inline DecimalText formatDecimal(uint64_t number, size_t min_digits = 1)
{
    constexpr uint64_t DECIMAL_BASE = 10;
    char digits[MAX_DECIMAL_DIGITS] {};
    size_t start = MAX_DECIMAL_DIGITS;
    uint64_t rest = number;
    do {
        --start;
        digits[start] = static_cast<char>('0' + rest % DECIMAL_BASE);
        rest /= DECIMAL_BASE;
    } while (rest > 0);
    const size_t width = min_digits < MAX_DECIMAL_DIGITS ? min_digits : MAX_DECIMAL_DIGITS;
    while (MAX_DECIMAL_DIGITS - start < width) {
        --start;
        digits[start] = '0';
    }
    DecimalText text {};
    text.assign(Span<const char>(&digits[start], MAX_DECIMAL_DIGITS - start));
    return text;
}

}
