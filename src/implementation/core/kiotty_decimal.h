#pragma once

#include "kiotty_spec.h"
#include "kiotty_span.h"

#include <cstddef>
#include <cstdint>

namespace kiotty::spec::implementation {

template <typename Text>
KiottySpecStatus appendDecimal(Text& text, uint64_t number, size_t min_digits = 1)
{
    constexpr size_t MAX_DIGITS = 20;
    constexpr uint64_t DECIMAL_BASE = 10;
    char digits[MAX_DIGITS] {};
    size_t count = 0;
    do {
        digits[count] = static_cast<char>('0' + number % DECIMAL_BASE);
        number /= DECIMAL_BASE;
        ++count;
    } while (number > 0);
    while (count < min_digits && count < MAX_DIGITS) { digits[count++] = '0'; }
    for (size_t index = count; index > 0; --index) {
        const KiottySpecStatus status = text.append(Span<const char>(&digits[index - 1], 1));
        if (status != KIOTTY_SPEC_STATUS_SUCCESS) { return status; }
    }
    return KIOTTY_SPEC_STATUS_SUCCESS;
}

}
